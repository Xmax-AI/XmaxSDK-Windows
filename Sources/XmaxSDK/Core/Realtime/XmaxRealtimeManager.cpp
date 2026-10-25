//
//  XmaxRealtimeManager.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Core/Realtime/XmaxRealtimeFactory.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Service/Media/MediaService.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {

/**
 * The lifecycle owner shared by realtime workflow methods.
 */
using Lifecycle = XmaxRealtimeLifecycleManager;
/**
 * The operation categories used for foreground resource admission.
 */
using OperationKind = Lifecycle::OperationKind;
/**
 * The resource boundaries used for coordinated termination.
 */
using Scope = Lifecycle::TerminationScope;

/**
 * Copies event text into a fixed-capacity buffer, preserving a trailing NUL.
 *
 * @param[out] out The writable event-text buffer.
 * @param[in] size The total buffer capacity in bytes, including the trailing NUL.
 * @param[in] text The UTF-8 text to copy into a fixed-capacity event field.
 */
static void text_copy(
    char* out,
    size_t size,
    const std::string& text
) {
    auto count = std::min(size - 1, text.size());

    while (count && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80)
        --count;

    std::memcpy(
        out,
        text.data(),
        count
    );
    out[count] = 0;
}

/**
 * Creates an independent realtime lifecycle controller with shared API services and native factories.
 *
 * @param[in] api The shared API service, retained for the controller's lifetime.
 * @param[in] factory The native adapter factory, which must not return an empty object.
 * @param[in] heartbeat The active-session heartbeat interval, in milliseconds.
 * @param[in] camera The native camera adapter factory.
 * @param[in] history Optional quality history scoped by model and direction; an empty reference disables
 * persistence.
 */
XmaxRealtimeManager::XmaxRealtimeManager(
    std::shared_ptr<ApiService> api,
    RtcFactory factory,
    std::chrono::milliseconds heartbeat,
    CameraFactory camera,
    std::shared_ptr<QualityHistory> history
)
    : api_(std::move(api)),
      connection_(api_),
      stream_(
          std::move(factory),
          std::move(history)
      ),
      media_(std::move(camera)),
      generation_(media_.interaction),
      heartbeat_interval_(heartbeat),
      lifecycle_(
          [this] { this->heartbeat(); },
          [this](Error error) { fail(std::move(error)); }
      ) {
    require(
        bool(api_),
        XMAX_INVALID_ARGUMENT,
        "API service is required"
    );
    require(
        heartbeat.count() > 0,
        XMAX_INVALID_ARGUMENT,
        "Invalid heartbeat interval"
    );
    XmaxRealtimeFactory::assemble(
        lifecycle_,
        errors_,
        [this](Scope scope) { return cleanup(scope); },
        [this](const RealtimeState& state) {
            std::lock_guard<std::mutex> lock(interaction_mutex_);
            interaction_task_ = state.connection_state == XMAX_GENERATING ? state.task_id : "";
            if (state.connection_state != XMAX_GENERATING)
                render_.trajectory.clear();
        }
    );
}

/**
 * Cancels in-flight work, closes media and RTC resources, and stops lifecycle execution.
 */
XmaxRealtimeManager::~XmaxRealtimeManager() {
    shutdown();
}

/**
 * Permanently closes native resources and drains application delivery before stopping lifecycle work.
 * Safe to repeat; source receivers remain inert even while an in-flight call retains this object.
 */
void XmaxRealtimeManager::shutdown() noexcept {
    std::call_once(
        shutdown_once_,
        [this] {
            try {
                disconnect(true);
            } catch (...) {
            }
            render_.shutdown_output();
            lifecycle_.shutdown();
        }
    );
}

/**
 * Updates session state and delivers its state event to your app.
 *
 * @param[in] state The new realtime lifecycle state.
 */
void XmaxRealtimeManager::change_state(
    xmax_state state
) {
    const auto previous = lifecycle_.snapshot();
    lifecycle_.commit({state, connection_.active().id.empty() ? previous.event.session_id : connection_.active().id, state == XMAX_GENERATING ? stream_.task() : ""});
}

/**
 * Stages final state and startup bookkeeping until the operation releases admission.
 *
 * @param[in] state The operation's successful phase.
 * @param[in] token The current operation token.
 * @param[in] completed Optional bookkeeping performed after state publication.
 */
void XmaxRealtimeManager::complete_state(
    xmax_state state,
    const Lifecycle::Token& token,
    std::function<void()> completed
) {
    const auto previous = lifecycle_.snapshot();
    lifecycle_.complete(
        {state,
         connection_.active().id.empty() ? previous.event.session_id : connection_.active().id,
         state == XMAX_GENERATING ? stream_.task() : ""},
        token,
        std::move(completed)
    );
}

/**
 * Binds external video input and enters the media-ready state.
 *
 * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
 * @param[in] has_audio Whether the source provides PCM upload without microphone capture.
 *
 * @return The revision identifying this external source binding.
 */
uint64_t XmaxRealtimeManager::prepare(
    xmax_video_format format,
    bool has_audio
) {
    uint64_t binding = 0;
    lifecycle_.run(
        OperationKind::Media,
        [this, format, has_audio, &binding](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_IDLE || lifecycle_.state() == XMAX_READY,
                XMAX_INVALID_STATE,
                "Disconnect before replacing an external source"
            );
            const auto resolved = MediaService::input(format);
            token.failure_scope(Scope::All);
            errors_.reset();
            change_state(XMAX_PREPARING);
            bind_local();
            media_.prepare_external(
                resolved,
                media_epoch_.load(),
                has_audio
            );
            token.ensure_current();
            binding = media_epoch_;
            camera_microphone_ = false;
            complete_state(
                XMAX_READY,
                token
            );
        }
    );
    return binding;
}

/**
 * Reports whether external input accepts preview and publication frames.
 *
 * @return True for a live external binding, including while disconnected; false for camera mode or failed input.
 */
bool XmaxRealtimeManager::requesting() const {
    return !media_.camera_active() && failed_media_epoch_ != media_epoch_.load() &&
           media_.external().bound();
}

/**
 * Binds local preview to a new media revision, rejecting earlier frames.
 */
void XmaxRealtimeManager::bind_local() {
    std::lock_guard<std::mutex> lock(media_binding_mutex_);
    const auto token = ++media_epoch_;
    failed_media_epoch_ = 0;
    render_.bind_local(token);
    media_.external().preview(
        [this, token](Frame frame) { render_.receive_local(
                                         std::move(frame),
                                         token
                                     ); },
        token
    );
}

/**
 * Creates a failure callback that accepts only the current media binding's errors.
 *
 * @param[in] token The connection or media-binding revision used to reject stale callbacks.
 *
 * @return The current binding's failure callback; errors from previous bindings are ignored.
 */
std::function<void(Error)> XmaxRealtimeManager::media_failure(
    uint64_t token
) {
    return [this, token](Error error) {
        errors_.forward(
            std::move(error),
            Scope::All,
            [this, token] {
                return media_epoch_ == token && media_.camera_active();
            }
        );
    };
}

/**
 * Invalidates local source callbacks and clears preview after media has stopped.
 */
void XmaxRealtimeManager::unbind_local() {
    std::lock_guard<std::mutex> lock(media_binding_mutex_);
    render_.unbind_local(++media_epoch_);
}

/**
 * Cancels work using a failed external source and schedules media, connection, and session cleanup.
 *
 * @param[in] binding The source revision; stale or already-failed bindings are ignored.
 * @param[in] error The source failure, copied and redacted before application delivery.
 */
void XmaxRealtimeManager::external_failure(
    uint64_t binding,
    Error error
) {
    {
        std::lock_guard<std::mutex> lock(media_binding_mutex_);
        if (binding == 0 || binding != media_epoch_ || failed_media_epoch_ == binding ||
            media_.camera_active() || !media_.external().bound())
            return;
        failed_media_epoch_ = binding;
    }
    errors_.forward(
        std::move(error),
        Scope::All,
        [this, binding] {
            return media_epoch_ == binding && failed_media_epoch_ == binding;
        }
    );
}

/**
 * Reports whether a source binding accepts frames for preview and publication.
 *
 * @param[in] binding The revision returned when this source was bound.
 *
 * @return True for a live binding, including while disconnected; false after failure or unbinding.
 */
bool XmaxRealtimeManager::requesting(
    uint64_t binding
) const {
    std::lock_guard<std::mutex> lock(media_binding_mutex_);
    return binding != 0 && media_epoch_ == binding && failed_media_epoch_ != binding &&
           !media_.camera_active() && media_.external().bound();
}

/**
 * Submits a source-bound external frame; replaced or closed bindings silently discard input.
 *
 * @param[in] frame The borrowed pixels, copied before this call returns.
 * @param[in] binding The revision returned when this source was bound.
 */
void XmaxRealtimeManager::push(
    const xmax_video_frame& frame,
    uint64_t binding
) {
    if (binding == 0 || media_epoch_ != binding || failed_media_epoch_ == binding || media_.camera_active())
        return;
    try {
        media_.external().push(
            frame,
            binding
        );
    } catch (const Error& error) {
        const Error source_error(
            XMAX_MEDIA_ERROR,
            error.what(),
            error.api_code,
            error.http_status
        );
        external_failure(
            binding,
            source_error
        );
        throw source_error;
    } catch (...) {
        const Error source_error(
            XMAX_MEDIA_ERROR,
            "Could not process external video pixels"
        );
        external_failure(
            binding,
            source_error
        );
        throw source_error;
    }
}

/**
 * Starts a camera, waits for its first frame, and enters ready state with native capture dimensions
 * preserved.
 *
 * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
 * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
void XmaxRealtimeManager::prepare_camera(
    std::string device,
    xmax_video_format format,
    bool microphone
) {
    lifecycle_.run(
        OperationKind::Media,
        [this, device = std::move(device), format, microphone](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_IDLE || lifecycle_.state() == XMAX_READY,
                XMAX_INVALID_STATE,
                "Disconnect before replacing local media"
            );
            const auto resolved = MediaService::camera_input(format);
            token.failure_scope(Scope::All);
            errors_.reset();
            change_state(XMAX_PREPARING);
            bind_local();
            media_.prepare_camera(
                device,
                resolved,
                media_failure(media_epoch_),
                token.cancellation()
            );
            token.ensure_current();
            camera_microphone_ = microphone;
            stream_.volume(0);
            complete_state(
                XMAX_READY,
                token
            );
        }
    );
}

/**
 * Releases local capture and input bindings after disconnection.
 */
void XmaxRealtimeManager::stop_media() {
    lifecycle_.run(
        OperationKind::Media,
        [this](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_IDLE || lifecycle_.state() == XMAX_READY,
                XMAX_INVALID_STATE,
                "Disconnect before stopping local media"
            );
            token.failure_scope(Scope::All);
            media_.close();
            unbind_local();
            camera_microphone_ = false;
            token.ensure_current();
            errors_.reset();
            complete_state(
                XMAX_IDLE,
                token
            );
        }
    );
}

/**
 * Copies and submits an external frame synchronously; may be called on a media thread.
 *
 * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
 * synchronous call returns.
 */
void XmaxRealtimeManager::push(
    const xmax_video_frame& frame
) {
    require(
        !media_.camera_active(),
        XMAX_INVALID_STATE,
        "External frames cannot replace SDK camera capture"
    );
    media_.external().push(frame);
}

/**
 * Creates a server session and joins RTC, rolling back allocated resources on cancellation or failure.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] epoch The cancellation revision captured when the operation starts, used to reject stale
 * work.
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
void XmaxRealtimeManager::connect_impl(
    const Cancel& cancel,
    uint64_t epoch,
    bool microphone
) {
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    require(
        lifecycle_.state() == XMAX_READY,
        XMAX_INVALID_STATE,
        "Prepare local media before connecting"
    );
    require(
        !api_->config().key.empty(),
        XMAX_INVALID_API_KEY,
        "An Xmax API Key is required"
    );
    errors_.reset();
    timing_.begin_session();
    change_state(XMAX_CONNECTING);

    connection_.create(
        stream_,
        cancel
    );
    timing_.finish_session();
    timing_.begin_join();

    RtcCallbacks callbacks;
    callbacks.published = [this, epoch, publication = timing_.publication()] {
        if (lifecycle_.epoch() != epoch)
            return;

        if (!RealtimeTiming::published(
                publication,
                Clock::now()
            ))
            return;

        lifecycle_.enqueue([this, epoch, publication] {
            if (lifecycle_.epoch() == epoch && lifecycle_.state() == XMAX_GENERATING &&
                timing_.update_publication(publication))
                emit_timing(timing_.snapshot());
        });
    };
    callbacks.frame = [this, epoch](Frame frame) {
        if (lifecycle_.epoch() == epoch)
            render_.receive(
                std::move(frame),
                epoch
            );
    };
    callbacks.audio = [this, epoch](AudioFrame frame) {
        if (lifecycle_.epoch() == epoch)
            render_.receive_audio(
                std::move(frame),
                epoch
            );
    };
    callbacks.statistics = [this, epoch](xmax_video_statistics statistics) {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        if (lifecycle_.epoch() == epoch && statistics.direction >= 0 && statistics.direction <= 1)
            statistics_[statistics.direction] = statistics;
    };
    callbacks.event = [this, epoch](xmax_event event) {
        if (lifecycle_.epoch() != epoch)
            return;
        lifecycle_.emit(event);
        if (event.type == XMAX_EVENT_STATISTICS && event.quality >= 0) {
            const double sampled = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
            lifecycle_.enqueue_adaptive([this, epoch, event, sampled](const Lifecycle::Token& token) {
                if (lifecycle_.epoch() != epoch || lifecycle_.state() != XMAX_GENERATING)
                    return;
                for (auto result : stream_.observe_quality(
                         event.direction,
                         event.quality,
                         sampled,
                         token.cancellation()
                     ))
                    lifecycle_.emit(result);
            });
        }
    };
    callbacks.failure = [this, epoch](Error error) {
        errors_.forward(
            std::move(error),
            Scope::Connection,
            [this, epoch] { return lifecycle_.epoch() == epoch; }
        );
    };
    stream_.external_audio(media_.external().has_audio());
    connection_.join(
        stream_,
        media_.external().format(),
        !media_.external().has_audio() && (microphone || camera_microphone_),
        std::move(callbacks),
        cancel,
        MediaService::generation(media_.external().format())
    );
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    render_.bind(epoch);
    timing_.begin_publication();
    media_.external().start(
        [this, connection_cancel = lifecycle_.cancellation(epoch)](const Frame& frame) {
            if (!connection_cancel())
                stream_.push(frame);
        },
        [this, connection_cancel = lifecycle_.cancellation(epoch)](const ExternalAudioPacket& frame) {
            if (!connection_cancel())
                stream_.push_audio(frame);
        }
    );
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    timing_.finish_join();
    lifecycle_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * Creates a server session and joins RTC without starting generation.
 *
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
void XmaxRealtimeManager::connect(
    bool microphone
) {
    lifecycle_.run(
        OperationKind::Connection,
        [this, microphone](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_READY,
                XMAX_INVALID_STATE,
                "Media is not ready or a connection already exists"
            );
            token.failure_scope(Scope::Connection);
            connect_impl(
                token.cancellation(),
                lifecycle_.epoch(),
                microphone
            );
            complete_state(
                XMAX_CONNECTED,
                token
            );
        }
    );
}

/**
 * Sends generation conditions, enables input delivery, and waits for the first result frame.
 *
 * @param[in] context The prompt, reference image, and face-restoration conditions for this generation.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 *
 * @return True when a new task reached its first result; false when existing conditions were updated.
 */
bool XmaxRealtimeManager::generate_impl(
    const Context& context,
    const Cancel& cancel
) {
    const auto maintain = [this, cancel] {
        if (Clock::now() >= lifecycle_.deadline())
            keepalive(cancel);
    };

    if (lifecycle_.state() == XMAX_GENERATING) {
        generation_.run(
            stream_,
            context,
            true,
            cancel,
            maintain
        );
        return false;
    }

    require(
        lifecycle_.state() == XMAX_CONNECTED,
        XMAX_INVALID_STATE,
        "No realtime connection is active"
    );

    timing_.begin_signal();
    render_.trajectory.clear();
    generation_.run(
        stream_,
        context,
        false,
        cancel,
        maintain
    );
    lifecycle_.deadline(Clock::now() + heartbeat_interval_);
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    timing_.finish_signal();
    return true;
}

/**
 * Connects if needed and starts generation, blocking until the first result frame.
 *
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
void XmaxRealtimeManager::start(
    std::string prompt,
    std::string reference,
    bool face,
    bool microphone
) {
    request_generation(
        Context{std::move(prompt), std::move(reference), face},
        microphone
    );
}

/**
 * Starts a task with supplied or cached conditions, blocking until the first result frame.
 *
 * @param[in] value Optional generation conditions; null reuses previous conditions, but the first call
 * requires them.
 */
void XmaxRealtimeManager::generate(
    const xmax_generation_context* value
) {
    std::optional<Context> incoming;

    if (value)
        incoming = Context{
            value->prompt,
            value->reference_path ? value->reference_path : "",
            value->face_restoration != 0
        };

    request_generation(
        std::move(incoming),
        false
    );
}

/**
 * Replaces the current task's generation conditions.
 *
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 */
void XmaxRealtimeManager::update(
    std::string prompt,
    std::string reference,
    bool face
) {
    require(
        lifecycle_.state() == XMAX_GENERATING,
        XMAX_INVALID_STATE,
        "No generation is active"
    );
    request_generation(
        Context{std::move(prompt), std::move(reference), face},
        false
    );
}

/**
 * Replaces an unfinished generation request, then executes after its cleanup on the lifecycle queue.
 *
 * @param[in] context Conditions to commit on success; an empty value reuses the last committed conditions.
 * @param[in] microphone Whether to enable microphone capture if a connection must be created.
 */
void XmaxRealtimeManager::request_generation(
    std::optional<Context> context,
    bool microphone
) {
    lifecycle_.run(
        OperationKind::Generation,
        [this, incoming = std::move(context), microphone](const Lifecycle::Token& token) {
            require(
                incoming.has_value() || generation_.context.has_value(),
                XMAX_INVALID_ARGUMENT,
                "Initial generation conditions are required"
            );
            require(
                lifecycle_.state() == XMAX_READY || lifecycle_.state() == XMAX_CONNECTED || lifecycle_.state() == XMAX_GENERATING,
                XMAX_INVALID_STATE,
                "Prepare local media before generation"
            );
            const auto value = incoming ? *incoming : *generation_.context;
            const auto previous_context = generation_.context;
            const auto cancel = token.cancellation();
            const auto retain_connection = lifecycle_.state() != XMAX_READY;
            const auto measures_startup = lifecycle_.state() != XMAX_GENERATING;
            token.failure_scope(Scope::Connection);
            if (measures_startup)
                timing_.begin();
            try {
                if (!retain_connection) {
                    connect_impl(
                        cancel,
                        lifecycle_.epoch(),
                        microphone
                    );
                    change_state(XMAX_CONNECTED);
                }
                const auto started = generate_impl(
                    value,
                    cancel
                );
                token.ensure_current();
                complete_state(
                    XMAX_GENERATING,
                    token,
                    [this, started] {
                        if (started) {
                            timing_.finish();
                            emit_timing(timing_.snapshot());
                        }
                    }
                );
            } catch (...) {
                generation_.context = previous_context;
                if (measures_startup) {
                    try {
                        throw;
                    } catch (const Error& error) {
                        if (const auto timing = timing_.failure(error))
                            emit_timing(*timing);
                    } catch (...) {
                        if (const auto timing = timing_.failure(Error(
                                XMAX_INTERNAL_ERROR,
                                "Generation startup failed"
                            )))
                            emit_timing(*timing);
                    }
                }
                throw;
            }
        }
    );
}

/**
 * Switches the active camera and resumes generation with retained conditions.
 *
 * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
 */
void XmaxRealtimeManager::switch_camera(
    std::string device
) {
    lifecycle_.run(
        OperationKind::CameraSwitch,
        [this, device = std::move(device)](const Lifecycle::Token& token) {
            const auto state = lifecycle_.state();
            require(
                media_.camera_active() && (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
                XMAX_INVALID_STATE,
                "No switchable camera stream is active"
            );
            token.failure_scope(Scope::All);
            const auto cancel = token.cancellation();
            const auto epoch = lifecycle_.epoch();
            media_.stop();
            if (state == XMAX_GENERATING) {
                stream_.stop_generation(cancel);
                change_state(XMAX_CONNECTED);
            }
            render_.unbind(epoch);
            bind_local();
            media_.switch_camera(
                device,
                media_failure(media_epoch_),
                cancel
            );
            token.ensure_current();
            if (state != XMAX_READY) {
                media_.external().start([this, connection_cancel = lifecycle_.cancellation(epoch)](const Frame& frame) {
                    if (!connection_cancel())
                        stream_.push(frame);
                });
                render_.bind(epoch);
                if (state == XMAX_GENERATING) {
                    timing_.begin();
                    generate_impl(
                        *generation_.context,
                        cancel
                    );
                }
            }
            complete_state(
                state,
                token,
                [this, state] {
                    if (state == XMAX_GENERATING) {
                        timing_.finish();
                        emit_timing(timing_.snapshot());
                    }
                }
            );
        }
    );
}

/**
 * Returns the session state, session identifier, and task identifier.
 */
xmax_event XmaxRealtimeManager::state_snapshot() {
    xmax_event result{};
    lifecycle_.sync([this, &result] { result = lifecycle_.snapshot().event; });
    return result;
}

/**
 * Returns the currently bound input video format.
 */
xmax_video_format XmaxRealtimeManager::format() {
    xmax_video_format result{};
    lifecycle_.sync([this, &result] { result = media_.external().format(); });
    return result;
}

/**
 * Returns the remote playback volume for the current or next connection.
 *
 * @return The playback volume in [0, 1], initially 0.
 */
float XmaxRealtimeManager::volume() {
    return stream_.volume();
}

/**
 * Configures frame interpolation; enabling requires a native backend.
 *
 * @param[in] enabled True to enable; false to disable.
 */
void XmaxRealtimeManager::interpolation(
    bool enabled
) {
    require(
        !enabled,
        XMAX_UNSUPPORTED,
        "This build has no native frame interpolation backend"
    );
}

/**
 * Validates and applies uplink bitrate limits and degradation preference.
 *
 * @param[in] value The uplink bitrate limits and degradation preference; see xmax_encoding_config for
 * units and special values.
 */
void XmaxRealtimeManager::encoding(
    xmax_encoding_config value
) {
    lifecycle_.run(
        OperationKind::Configuration,
        [this, value](const Lifecycle::Token&) { stream_.encoding(value); }
    );
}

/**
 * Commits RTC publication mirroring before display settings, retaining the previous configuration on
 * failure.
 *
 * @param[in] value The local preview, remote display, and published-video mirror settings.
 */
void XmaxRealtimeManager::video_mirror(
    xmax_video_mirror_config value
) {
    require(
        (value.local_preview == 0 || value.local_preview == 1) &&
            (value.remote_view == 0 || value.remote_view == 1) &&
            (value.published_video == 0 || value.published_video == 1),
        XMAX_INVALID_ARGUMENT,
        "Mirror settings must be 0 or 1"
    );
    lifecycle_.run(
        OperationKind::Configuration,
        [this, value](const Lifecycle::Token&) {
            stream_.mirror(value.published_video != 0);
            render_.mirror(
                value.local_preview != 0,
                value.remote_view != 0
            );
            mirror_ = value;
            mirror_.struct_size = sizeof(mirror_);
        }
    );
}

/**
 * Returns the mirror configuration retained for the current and next connection.
 */
xmax_video_mirror_config XmaxRealtimeManager::video_mirror() {
    xmax_video_mirror_config value{};
    lifecycle_.sync([this, &value] { value = mirror_; });
    return value;
}

/**
 * Enables or disables quality adaptation and resets the network observation window.
 *
 * @param[in] enabled True to enable; false to disable.
 */
void XmaxRealtimeManager::adaptive(
    bool enabled
) {
    lifecycle_.run(
        OperationKind::Configuration,
        [this, enabled](const Lifecycle::Token&) { stream_.adaptive(enabled); }
    );
}

/**
 * Replaces the serialized remote audio/video listener, preserving playback and the current listener
 * if native audio observation cannot be enabled.
 *
 * @param[in] listener The owned-packet callback, or an empty function to discard queued output and stop
 * observation. An in-flight callback may finish after this method returns.
 */
void XmaxRealtimeManager::media_listener(
    RemoteMediaListener listener
) {
    RemoteMediaListener previous;
    lifecycle_.sync([this, &previous, listener = std::move(listener)]() mutable {
        if (listener) {
            stream_.observe_remote_media(true);
            previous = render_.media_listener(std::move(listener));
        } else {
            previous = render_.media_listener({});
            stream_.observe_remote_media(false);
        }
    });
}

/**
 * Returns the current uplink and downlink quality levels and effective video formats.
 *
 * @return The adaptation setting, directional quality levels, and effective dimensions and frame rates.
 */
xmax_quality_status XmaxRealtimeManager::quality() {
    xmax_quality_status value{};
    lifecycle_.sync([this, &value] { value = stream_.quality(); });
    return value;
}

/**
 * Releases the resource scope selected by lifecycle termination without owning state transitions.
 *
 * @param[in] scope The generation, connection, or full media cleanup scope.
 *
 * @return The remaining local-media availability, session identifier, and release outcome.
 */
Lifecycle::CleanupResult XmaxRealtimeManager::cleanup(
    Scope scope
) {
    if (scope == Scope::Generation) {
        stream_.stop_generation([] { return false; });
        generation_.reset(false);
        render_.trajectory.clear();
        lifecycle_.deadline(Clock::now() + heartbeat_interval_);
        return {connection_.active().id, media_.external().bound(), true};
    }
    const bool close = scope == Scope::All;

    generation_.reset(false);
    render_.trajectory.clear();
    media_.stop();
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_[0] = statistics_[1] = {};
    }
    render_.unbind(lifecycle_.epoch());
    lifecycle_.deadline(Clock::time_point::max());

    timing_.reset_publication();

    bool server_closed = true;
    const auto session_id = connection_.active().id;

    try {
        connection_.close(stream_);
    } catch (...) {
        server_closed = false;
        Error error(
            XMAX_API_ERROR,
            "Server session close failed"
        );
        try {
            throw;
        } catch (const Error& original) {
            error = original;
        } catch (...) {
        }
        if (errors_.reason() != XMAX_REASON_FAILURE) {
            errors_.record(error);
        }
    }

    if (close) {
        media_.close();
        unbind_local();
        camera_microphone_ = false;
        generation_.context.reset();
    }

    return {session_id, media_.external().bound(), server_closed};
}

/**
 * Cancels in-flight work and disconnects, optionally closing local media.
 *
 * @param[in] close Whether to also release local media and cached generation conditions.
 */
void XmaxRealtimeManager::disconnect(
    bool close
) {
    const auto closed = lifecycle_.terminate(close ? Scope::All : Scope::Connection);
    if (!closed) {
        const auto failure = lifecycle_.snapshot().error;
        throw Error(
            XMAX_API_ERROR,
            "Server session close failed; inspect the session ID in the error event",
            failure.api_code,
            failure.http_status
        );
    }
}

/**
 * Releases resources held by a failed operation and delivers the original SDK error.
 *
 * @param[in] error The original SDK exception whose code and diagnostic must be preserved.
 */
void XmaxRealtimeManager::fail(
    Error error
) {
    errors_.forward(
        std::move(error),
        Scope::Connection
    );
}

/**
 * Runs due server-session and RTC room keepalives for an active connection.
 */
void XmaxRealtimeManager::heartbeat() {
    const auto cancel = lifecycle_.cancellation(lifecycle_.epoch());
    try {
        keepalive(cancel);
    } catch (const Error& error) {
        if (error.code != XMAX_CANCELLED)
            fail(error);
        else
            lifecycle_.deadline(Clock::time_point::max());
    } catch (const std::exception&) {
        fail(Error(
            XMAX_API_ERROR,
            "Session heartbeat failed"
        ));
    }
}

/**
 * Checks cancellation and keeps the session and room alive while waiting for generation.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void XmaxRealtimeManager::keepalive(
    const Cancel& cancel
) {
    connection_.heartbeat(
        stream_,
        cancel
    );
    lifecycle_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * Sets and retains the remote playback volume for the current or next connection.
 *
 * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
 *
 * @note Retains playback configuration independently of connection and generation operations.
 */
void XmaxRealtimeManager::volume(
    float value
) {
    stream_.volume(value);
}

/**
 * Updates a ready, disconnected camera's encoding format, attempting to restore the previous format on
 * failure.
 *
 * @param[in] format The target dimensions and frame rate supported by the model.
 */
void XmaxRealtimeManager::update_camera_format(
    xmax_video_format format
) {
    lifecycle_.run(
        OperationKind::Media,
        [this, format](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_READY && media_.camera_active(),
                XMAX_INVALID_STATE,
                "Camera format can only be updated before connection"
            );

            const auto resolved = MediaService::camera_input(format);
            errors_.reset();
            change_state(XMAX_PREPARING);

            try {
                media_.update_camera_format(
                    resolved,
                    media_failure(media_epoch_),
                    token.cancellation()
                );
                token.ensure_current();
                complete_state(
                    XMAX_READY,
                    token
                );
            } catch (const Error& error) {
                errors_.record(error);
                if (!media_.camera_active())
                    unbind_local();
                change_state(media_.camera_active() ? XMAX_READY : XMAX_IDLE);
                throw;
            }
        }
    );
}

/**
 * Returns the camera format, or an all-zero format if no camera stream exists.
 */
xmax_video_format XmaxRealtimeManager::camera_format() {
    xmax_video_format result{};
    lifecycle_.sync([this, &result] {
        if (media_.camera_active())
            result = media_.external().format();
    });
    return result;
}

/**
 * Applies a directional quality level during generation without locking out subsequent adaptation.
 *
 * @param[in] direction 0 for uplink; 1 for downlink.
 * @param[in] level The target quality level, in [1, 5].
 */
void XmaxRealtimeManager::quality_level(
    int direction,
    int level
) {
    lifecycle_.run(
        OperationKind::Configuration,
        [this, direction, level](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_GENERATING,
                XMAX_INVALID_STATE,
                "Quality levels can only be changed during generation"
            );
            const auto cancel = token.cancellation();
            lifecycle_.emit(stream_.set_quality(
                direction,
                level,
                cancel,
                [this, cancel] {
                    if (Clock::now() >= lifecycle_.deadline())
                        keepalive(cancel);
                }
            ));
        }
    );
}

/**
 * Returns the current state snapshot with its reason and error.
 */
xmax_event_info XmaxRealtimeManager::state_info() {
    xmax_event_info result{};
    lifecycle_.sync([this, &result] { result = lifecycle_.snapshot(); });
    return result;
}

/**
 * Disconnects for the specified reason while retaining local media.
 *
 * @param[in] reason The normal-stop or orientation-change reason.
 */
void XmaxRealtimeManager::disconnect_reason(
    xmax_realtime_reason reason
) {
    require(
        reason == XMAX_REASON_NORMAL || reason == XMAX_REASON_ORIENTATION_CHANGED,
        XMAX_INVALID_ARGUMENT,
        "Failure reasons are reported by the SDK"
    );
    const auto closed = lifecycle_.terminate(
        Scope::Connection,
        reason
    );
    if (!closed) {
        const auto failure = lifecycle_.snapshot().error;
        throw Error(
            XMAX_API_ERROR,
            "Server session close failed; inspect the session ID in the error event",
            failure.api_code,
            failure.http_status
        );
    }
}

/**
 * Copies viewport trajectory samples and schedules asynchronous delivery, retaining only the latest
 * pending sample.
 *
 * @param[in] frame The sampled points and actual video layout.
 */
void XmaxRealtimeManager::submit_interaction(
    const xmax_interaction_frame& frame
) {
    auto points = InteractionCoordinateMapper::map(frame);
    if (points.empty())
        return;
    const auto epoch = lifecycle_.epoch();
    std::lock_guard<std::mutex> lock(interaction_mutex_);
    require(
        !interaction_task_.empty() && lifecycle_.state() == XMAX_GENERATING,
        XMAX_INVALID_STATE,
        "No generation is active"
    );
    render_.trajectory.update(points);
    if (media_.interaction.submit({epoch, interaction_task_, std::move(points)}))
        lifecycle_.enqueue([this] { drain_interaction(); });
}

/**
 * Schedules one trajectory sample fairly so continuous input cannot monopolize lifecycle execution.
 */
void XmaxRealtimeManager::drain_interaction() {
    auto sample = media_.interaction.take();
    if (!sample)
        return;

    const auto epoch = lifecycle_.epoch();
    const auto cancel = lifecycle_.cancellation(epoch);
    try {
        if (lifecycle_.state() == XMAX_GENERATING)
            generation_.send_interaction(
                stream_,
                *sample,
                epoch,
                cancel,
                [this, cancel] {
                    if (Clock::now() >= lifecycle_.deadline())
                        keepalive(cancel);
                }
            );
    } catch (const Error& error) {
        if (error.code != XMAX_CANCELLED) {
            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.code = error.code;
            event.state = lifecycle_.state();
            text_copy(
                event.message,
                sizeof(event.message),
                XmaxLogger::redact(error.what())
            );
            lifecycle_.emit(
                event,
                XMAX_REASON_FAILURE,
                &error
            );
        }
    }

    lifecycle_.enqueue([this] { drain_interaction(); });
}

/**
 * Converts and submits source-bound PCM; stale or disconnected audio is discarded.
 *
 * @param[in] frame The borrowed PCM data on the source video timeline.
 * @param[in] binding The source revision returned at binding.
 *
 * @throws Error If current PCM input is invalid; the binding is also failed and cleaned up.
 */
void XmaxRealtimeManager::push_audio(
    const xmax_external_audio_frame& frame,
    uint64_t binding
) {
    if (binding == 0 || media_epoch_ != binding || failed_media_epoch_ == binding || media_.camera_active())
        return;
    try {
        media_.external().push_audio(
            frame,
            binding
        );
    } catch (const Error& error) {
        external_failure(
            binding,
            error
        );
        throw;
    } catch (...) {
        const Error error(
            XMAX_MEDIA_ERROR,
            "Could not process external PCM samples"
        );
        external_failure(
            binding,
            error
        );
        throw error;
    }
}

/**
 * Returns the latest directional video statistics without consuming events.
 *
 * @param[in] direction Zero for uplink and one for downlink.
 *
 * @return A current snapshot, or available = 0 with unavailable metrics set to -1.
 */
xmax_video_statistics XmaxRealtimeManager::video_statistics(
    int direction
) const {
    require(
        direction == 0 || direction == 1,
        XMAX_INVALID_ARGUMENT,
        "Invalid statistics direction"
    );
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    auto result = statistics_[direction];
    result.struct_size = sizeof(result);
    result.direction = direction;
    if (!result.available) {
        result.width = result.height = result.capture_fps = result.encode_fps = result.send_fps =
            result.decode_fps = result.render_fps = result.bitrate_kbps = result.packet_loss_percent =
                result.e2e_ms = -1;
    }
    return result;
}

/**
 * Publishes a timing snapshot with the current lifecycle state and session/task identities.
 *
 * @param[in] event The completed or failed startup timing snapshot.
 */
void XmaxRealtimeManager::emit_timing(
    xmax_event event
) {
    event.state = lifecycle_.state();
    copy_utf8(
        event.session_id,
        sizeof(event.session_id),
        lifecycle_.snapshot().event.session_id
    );
    copy_utf8(
        event.task_id,
        sizeof(event.task_id),
        stream_.task()
    );
    lifecycle_.emit(event);
}
} // namespace xmax::detail
