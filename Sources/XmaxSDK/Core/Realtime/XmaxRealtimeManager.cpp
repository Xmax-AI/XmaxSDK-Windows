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
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Service/Media/MediaService.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {

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
    : api_(std::move(api)), connection_(api_), stream_(
        std::move(factory),
        std::move(history)
    ),
      media_(std::move(camera)), heartbeat_interval_(heartbeat),
      coordinator_(
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
            coordinator_.shutdown();
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
    {
        std::lock_guard<std::mutex> lock(interaction_mutex_);
        interaction_task_ = state == XMAX_GENERATING ? stream_.task() : "";
        if (state != XMAX_GENERATING)
            render_.trajectory.clear();
    }
    coordinator_.state(state);
    if (!connection_.active().id.empty())
        last_session_id_ = connection_.active().id;
    if (!stream_.task().empty())
        last_task_id_ = stream_.task();

    xmax_event event{};
    event.type = XMAX_EVENT_STATE;
    event.state = state;
    event.code = errors_.error() ? errors_.error()->code : XMAX_OK;
    text_copy(
        event.session_id,
        sizeof(event.session_id),
        last_session_id_
    );
    text_copy(
        event.task_id,
        sizeof(event.task_id),
        last_task_id_
    );
    coordinator_.emit(
        event,
        errors_.reason(),
        errors_.error()
    );
    XmaxLogger::write(
        LogCategory::Realtime,
        XMAX_LOG_INFO,
        [&](auto language) {
            static const char*
                names[]{"idle", "ready", "connecting", "connected", "generating", "disconnecting", "preparing"};
            return XmaxLogger::localized(
                language,
                "状态更新 (State Changed)",
                "State Changed"
            ) +
                   "\n├─ " +
                   names[state] + "\n├─ session=" + connection_.active().id + "\n└─ task=" + stream_.task();
        }
    );
}

/**
 * Binds external video input and enters the media-ready state.
 *
 * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
 *
 * @return The revision identifying this external source binding.
 */
uint64_t XmaxRealtimeManager::prepare(
    xmax_video_format format
) {
    uint64_t binding = 0;
    coordinator_.sync([this, format, &binding] {
        require(
            coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY,
            XMAX_INVALID_STATE,
            "Disconnect before replacing an external source"
        );

        const auto resolved = MediaService::input(format);
        require(
            resolved.width >= 4 && resolved.height >= 4,
            XMAX_INVALID_ARGUMENT,
            "External video dimensions must be at least 4 pixels"
        );
        errors_.reset();
        change_state(XMAX_PREPARING);
        bind_local();
        media_.prepare_external(
            resolved,
            media_epoch_.load()
        );
        binding = media_epoch_;
        camera_microphone_ = false;
        change_state(XMAX_READY);
    });
    return binding;
}

/**
 * Reports whether external input requests frames without waiting for lifecycle work.
 *
 * @return True when external input requests frames; false for camera mode or no request.
 */
bool XmaxRealtimeManager::requesting() const {
    return !media_.camera_active() && input_epoch_.load() == coordinator_.epoch() &&
           media_.external().requesting();
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
        coordinator_.enqueue([this, token, error] {
            if (media_epoch_ != token || !media_.camera_active())
                return;

            errors_.record(error);
            coordinator_.invalidate();
            cleanup(true);

            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.code = error.code;
            event.state = coordinator_.state();
            text_copy(
                event.message,
                sizeof(event.message),
                XmaxLogger::redact(error.what())
            );
            coordinator_.emit(
                event,
                XMAX_REASON_FAILURE,
                &error
            );
        });
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
        ++generation_request_;
        coordinator_.invalidate();
    }
    coordinator_.enqueue([this, binding, error = std::move(error)] {
        if (media_epoch_ != binding)
            return;

        errors_.record(error);
        cleanup(true);

        xmax_event event{};
        event.type = XMAX_EVENT_ERROR;
        event.code = error.code;
        event.state = coordinator_.state();
        text_copy(
            event.message,
            sizeof(event.message),
            XmaxLogger::redact(error.what())
        );
        coordinator_.emit(
            event,
            XMAX_REASON_FAILURE,
            &error
        );
    });
}

/**
 * Reports whether a particular external source currently supplies the active connection.
 *
 * @param[in] binding The revision returned when this source was bound.
 *
 * @return False for stale bindings, camera mode, or inactive publication.
 */
bool XmaxRealtimeManager::requesting(
    uint64_t binding
) const {
    return binding != 0 && media_epoch_ == binding && requesting();
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
    if (binding == 0 || media_epoch_ != binding || media_.camera_active())
        return;
    media_.external().push(
        frame,
        binding
    );
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
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, device = std::move(device), format, microphone, epoch] {
        require(
            !coordinator_.cancellation(epoch)(),
            XMAX_CANCELLED,
            "Camera startup cancelled"
        );
        require(
            coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY,
            XMAX_INVALID_STATE,
            "Disconnect before replacing local media"
        );

        const auto resolved = MediaService::camera_input(format);
        errors_.reset();
        change_state(XMAX_PREPARING);
        bind_local();

        try {
            media_.prepare_camera(
                device,
                resolved,
                media_failure(media_epoch_),
                coordinator_.cancellation(epoch)
            );
            camera_microphone_ = microphone;
            stream_.volume(0);
            change_state(XMAX_READY);
        } catch (...) {
            try {
                throw;
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(
                    XMAX_INTERNAL_ERROR,
                    "Camera preparation failed"
                ));
            }
            media_.close();
            unbind_local();
            change_state(XMAX_IDLE);
            throw;
        }
    });
}

/**
 * Releases local capture and input bindings after disconnection.
 */
void XmaxRealtimeManager::stop_media() {
    coordinator_.sync([this] {
        require(
            coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY,
            XMAX_INVALID_STATE,
            "Disconnect before stopping local media"
        );
        media_.close();
        unbind_local();
        camera_microphone_ = false;
        change_state(XMAX_IDLE);
    });
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
 * @param[in] epoch The cancellation revision captured when the operation starts, used to reject stale work.
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
        coordinator_.state() == XMAX_READY,
        XMAX_INVALID_STATE,
        "Prepare local media before connecting"
    );
    require(
        !api_->config().key.empty(),
        XMAX_INVALID_API_KEY,
        "An Xmax API Key is required"
    );
    errors_.reset();
    timing_.begin();
    change_state(XMAX_CONNECTING);

    connection_.create(
        stream_,
        cancel
    );
    timing_.finish_session();
    timing_.begin_join();

    RtcCallbacks callbacks;
    callbacks.published = [this, epoch, publication = timing_.publication()] {
        if (coordinator_.epoch() != epoch)
            return;

        if (!RealtimeTiming::published(
            publication,
            Clock::now()
        ))
            return;

        coordinator_.enqueue([this, epoch, publication] {
            if (coordinator_.epoch() == epoch && coordinator_.state() == XMAX_GENERATING &&
                timing_.update_publication(publication))
                coordinator_.emit(timing_.snapshot());
        });
    };
    callbacks.frame = [this, epoch](Frame frame) {
        if (coordinator_.epoch() == epoch)
            render_.receive(
                std::move(frame),
                epoch
            );
    };
    callbacks.audio = [this, epoch](AudioFrame frame) {
        if (coordinator_.epoch() == epoch)
            render_.receive_audio(
                std::move(frame),
                epoch
            );
    };
    callbacks.event = [this, epoch](xmax_event event) {
        if (coordinator_.epoch() != epoch)
            return;
        coordinator_.emit(event);
        if (event.type == XMAX_EVENT_STATISTICS && event.quality >= 0) {
            const double sampled = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
            coordinator_.enqueue([this, epoch, event, sampled] {
                if (coordinator_.epoch() != epoch || coordinator_.state() != XMAX_GENERATING)
                    return;
                for (auto result : stream_.observe_quality(
                    event.direction,
                    event.quality,
                    sampled,
                    coordinator_.cancellation(epoch)
                ))
                    coordinator_.emit(result);
            });
        }
    };
    callbacks.failure = [this, epoch](Error error) {
        coordinator_.enqueue([this, epoch, error] {
            if (coordinator_.epoch() == epoch && !connection_.active().id.empty())
                fail(error);
        });
    };
    connection_.join(
        stream_,
        media_.external().format(),
        microphone || camera_microphone_,
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
    input_epoch_ = epoch;
    timing_.begin_publication();
    media_.external().start([this, connection_cancel = coordinator_.cancellation(epoch)](const Frame& frame) {
        if (!connection_cancel())
            stream_.push(frame);
    });
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    timing_.finish_join();
    change_state(XMAX_CONNECTED);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * Creates a server session and joins RTC without starting generation.
 *
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
void XmaxRealtimeManager::connect(
    bool microphone
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, microphone, epoch] {
        require(
            coordinator_.state() == XMAX_READY,
            XMAX_INVALID_STATE,
            "Media is not ready or a connection already exists"
        );

        try {
            connect_impl(
                coordinator_.cancellation(epoch),
                epoch,
                microphone
            );
        } catch (...) {
            auto original = std::current_exception();
            try {
                std::rethrow_exception(original);
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(
                    XMAX_INTERNAL_ERROR,
                    "Realtime operation failed"
                ));
            }

            coordinator_.invalidate();
            cleanup(false);
            std::rethrow_exception(original);
        }
    });
}

/**
 * Sends generation conditions, enables input delivery, and waits for the first result frame.
 *
 * @param[in] context The prompt, reference image, and face-restoration conditions for this generation.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void XmaxRealtimeManager::generate_impl(
    const Context& context,
    const Cancel& cancel
) {
    const auto maintain = [this, cancel] {
        if (Clock::now() >= coordinator_.deadline())
            keepalive(cancel);
    };

    if (coordinator_.state() == XMAX_GENERATING) {
        generation_.run(
            stream_,
            context,
            true,
            cancel,
            maintain
        );
        return;
    }

    require(
        coordinator_.state() == XMAX_CONNECTED,
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
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    timing_.finish_signal();
    coordinator_.emit(timing_.snapshot());
    change_state(XMAX_GENERATING);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
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
        coordinator_.state() == XMAX_GENERATING,
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
    const auto request = ++generation_request_;
    coordinator_.sync([this, incoming = std::move(context), microphone, request] {
        require(
            generation_request_ == request,
            XMAX_CANCELLED,
            "Generation request superseded or cancelled"
        );
        require(
            incoming.has_value() || generation_.context.has_value(),
            XMAX_INVALID_ARGUMENT,
            "Initial generation conditions are required"
        );
        require(
            coordinator_.state() == XMAX_READY || coordinator_.state() == XMAX_CONNECTED ||
                coordinator_.state() == XMAX_GENERATING,
            XMAX_INVALID_STATE,
            "Prepare local media before generation"
        );

        const auto value = incoming ? *incoming : *generation_.context;
        const auto previous_context = generation_.context;
        const auto epoch = coordinator_.epoch();
        const auto cancel = [this, epoch, request] {
            return coordinator_.epoch() != epoch || generation_request_ != request;
        };
        const auto retain_connection = coordinator_.state() != XMAX_READY;
        try {
            if (!retain_connection)
                connect_impl(
                    cancel,
                    epoch,
                    microphone
                );
            generate_impl(
                value,
                cancel
            );
            require(
                !cancel(),
                XMAX_CANCELLED,
                "Generation request superseded or cancelled"
            );
        } catch (...) {
            generation_.context = previous_context;
            const auto original = std::current_exception();
            const bool replaced = generation_request_ != request && coordinator_.epoch() == epoch;
            if (replaced && retain_connection) {
                try {
                    stream_.stop_generation(coordinator_.cancellation(epoch));
                    generation_.reset(false);
                    render_.trajectory.clear();
                    errors_.reset();
                    change_state(XMAX_CONNECTED);
                    coordinator_.deadline(Clock::now() + heartbeat_interval_);
                } catch (...) {
                    coordinator_.invalidate();
                    cleanup(false);
                }
            } else {
                try {
                    std::rethrow_exception(original);
                } catch (const Error& error) {
                    if (error.code != XMAX_CANCELLED)
                        errors_.record(error);
                } catch (...) {
                    errors_.record(Error(
                        XMAX_INTERNAL_ERROR,
                        "Realtime operation failed"
                    ));
                }
                coordinator_.invalidate();
                cleanup(false);
            }
            std::rethrow_exception(original);
        }
    });
}

/**
 * Switches the active camera and resumes generation with retained conditions.
 *
 * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
 */
void XmaxRealtimeManager::switch_camera(
    std::string device
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, device = std::move(device), epoch] {
        const auto state = coordinator_.state();
        require(
            media_.camera_active() &&
                (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
            XMAX_INVALID_STATE,
            "No switchable camera stream is active"
        );

        const auto cancel = coordinator_.cancellation(epoch);

        try {
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

            if (state != XMAX_READY) {
                media_.external().start([this, cancel](const Frame& frame) {
                    if (!cancel())
                        stream_.push(frame);
                });
                render_.bind(epoch);
                if (state == XMAX_GENERATING)
                    generate_impl(
                        *generation_.context,
                        cancel
                    );
            }
        } catch (...) {
            auto original = std::current_exception();
            try {
                std::rethrow_exception(original);
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(
                    XMAX_INTERNAL_ERROR,
                    "Realtime operation failed"
                ));
            }
            coordinator_.invalidate();
            cleanup(true);
            std::rethrow_exception(original);
        }
    });
}

/**
 * Returns the session state, session identifier, and task identifier.
 */
xmax_event XmaxRealtimeManager::state_snapshot() {
    xmax_event event{};
    coordinator_.sync([this, &event] {
        event.struct_size = sizeof(event);
        event.type = XMAX_EVENT_STATE;
        event.state = coordinator_.state();
        text_copy(
            event.session_id,
            sizeof(event.session_id),
            last_session_id_
        );
        text_copy(
            event.task_id,
            sizeof(event.task_id),
            last_task_id_
        );
    });
    return event;
}

/**
 * Returns the currently bound input video format.
 */
xmax_video_format XmaxRealtimeManager::format() {
    xmax_video_format result{};
    coordinator_.sync([this, &result] { result = media_.external().format(); });
    return result;
}

/**
 * Returns the remote playback volume for the current or next connection.
 *
 * @return The playback volume in [0, 1], initially 0.
 */
float XmaxRealtimeManager::volume() {
    float result = 0;
    coordinator_.sync([this, &result] { result = stream_.volume(); });
    return result;
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
 * @param[in] value The uplink bitrate limits and degradation preference; see xmax_encoding_config for units
 * and special values.
 */
void XmaxRealtimeManager::encoding(
    xmax_encoding_config value
) {
    coordinator_.sync([this, value] { stream_.encoding(value); });
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
    coordinator_.sync([this, value] {
        stream_.mirror(value.published_video != 0);
        render_.mirror(
            value.local_preview != 0,
            value.remote_view != 0
        );
        mirror_ = value;
        mirror_.struct_size = sizeof(mirror_);
    });
}

/**
 * Returns the mirror configuration retained for the current and next connection.
 */
xmax_video_mirror_config XmaxRealtimeManager::video_mirror() {
    xmax_video_mirror_config value{};
    coordinator_.sync([this, &value] { value = mirror_; });
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
    coordinator_.sync([this, enabled] { stream_.adaptive(enabled); });
}

/**
 * Replaces the serialized remote audio/video listener, preserving playback and the current listener if
 * native audio observation cannot be enabled.
 *
 * @param[in] listener The owned-packet callback, or an empty function to discard queued output and stop
 * observation. An in-flight callback may finish after this method returns.
 */
void XmaxRealtimeManager::media_listener(
    RemoteMediaListener listener
) {
    RemoteMediaListener previous;
    coordinator_.sync([this, &previous, listener = std::move(listener)]() mutable {
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
    coordinator_.sync([this, &value] { value = stream_.quality(); });
    return value;
}

/**
 * Cleans up RTC and the server session, optionally releasing media and generation conditions.
 *
 * @param[in] close Whether to also release local media and cached generation conditions.
 *
 * @return True if the server session closed; false if closing failed, with an error event delivered.
 */
bool XmaxRealtimeManager::cleanup(
    bool close
) {
    generation_.reset(false);
    render_.trajectory.clear();
    media_.stop();
    render_.unbind(coordinator_.epoch());
    coordinator_.deadline(Clock::time_point::max());

    if (!connection_.active().id.empty())
        change_state(XMAX_DISCONNECTING);

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
        xmax_event event{};
        event.type = XMAX_EVENT_ERROR;
        event.code = error.code;
        text_copy(
            event.session_id,
            sizeof(event.session_id),
            session_id
        );
        text_copy(
            event.message,
            sizeof(event.message),
            XmaxLogger::redact(error.what())
        );
        coordinator_.emit(
            event,
            XMAX_REASON_FAILURE,
            &error
        );
    }

    if (close) {
        media_.close();
        unbind_local();
        camera_microphone_ = false;
        generation_.context.reset();
    }

    change_state(media_.external().bound() ? XMAX_READY : XMAX_IDLE);

    return server_closed;
}

/**
 * Cancels in-flight work and disconnects, optionally closing local media.
 *
 * @param[in] close Whether to also release local media and cached generation conditions.
 */
void XmaxRealtimeManager::disconnect(
    bool close
) {
    ++generation_request_;
    const auto epoch = coordinator_.invalidate();
    media_.stop();
    render_.unbind(epoch);
    coordinator_.sync([this, close] {
        errors_.reset();
        if (!cleanup(close))
            throw Error(
                XMAX_API_ERROR,
                "Server session close failed; inspect the session ID in the error event",
                errors_.error() ? errors_.error()->api_code : 0,
                errors_.error() ? errors_.error()->http_status : 0
            );
    });
}

/**
 * Releases resources held by a failed operation and delivers the original SDK error.
 *
 * @param[in] error The original SDK exception whose code and diagnostic must be preserved.
 */
void XmaxRealtimeManager::fail(
    Error error
) {
    ++generation_request_;
    coordinator_.invalidate();
    errors_.record(error);
    cleanup(false);

    xmax_event event{};
    event.type = XMAX_EVENT_ERROR;
    event.code = error.code;
    event.state = coordinator_.state();
    text_copy(
        event.message,
        sizeof(event.message),
        XmaxLogger::redact(error.what())
    );
    coordinator_.emit(
        event,
        XMAX_REASON_FAILURE,
        &error
    );
}

/**
 * Runs due server-session and RTC room keepalives for an active connection.
 */
void XmaxRealtimeManager::heartbeat() {
    const auto cancel = coordinator_.cancellation(coordinator_.epoch());
    try {
        keepalive(cancel);
    } catch (const Error& error) {
        if (error.code != XMAX_CANCELLED)
            fail(error);
        else
            coordinator_.deadline(Clock::time_point::max());
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
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * Sets and retains the remote playback volume for the current or next connection.
 *
 * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
 */
void XmaxRealtimeManager::volume(
    float value
) {
    coordinator_.sync([this, value] { stream_.volume(value); });
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
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, format, epoch] {
        require(
            coordinator_.state() == XMAX_READY && media_.camera_active(),
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
                coordinator_.cancellation(epoch)
            );
            change_state(XMAX_READY);
        } catch (const Error& error) {
            errors_.record(error);
            change_state(media_.camera_active() ? XMAX_READY : XMAX_IDLE);
            throw;
        }
    });
}

/**
 * Returns the camera format, or an all-zero format if no camera stream exists.
 */
xmax_video_format XmaxRealtimeManager::camera_format() {
    xmax_video_format result{};
    coordinator_.sync([this, &result] {
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
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, direction, level, epoch] {
        require(
            coordinator_.state() == XMAX_GENERATING,
            XMAX_INVALID_STATE,
            "Quality levels can only be changed during generation"
        );
        const auto cancel = coordinator_.cancellation(epoch);
        coordinator_.emit(stream_.set_quality(
            direction,
            level,
            cancel,
            [this, cancel] {
                if (Clock::now() >= coordinator_.deadline())
                    keepalive(cancel);
            }
        ));
    });
}

/**
 * Returns the current state snapshot with its reason and error.
 */
xmax_event_info XmaxRealtimeManager::state_info() {
    xmax_event_info info{};
    coordinator_.sync([this, &info] {
        info.struct_size = sizeof(info);
        info.event.struct_size = sizeof(info.event);
        info.event.type = XMAX_EVENT_STATE;
        info.event.state = coordinator_.state();
        info.event.code = errors_.error() ? errors_.error()->code : XMAX_OK;
        text_copy(
            info.event.session_id,
            sizeof(info.event.session_id),
            last_session_id_
        );
        text_copy(
            info.event.task_id,
            sizeof(info.event.task_id),
            last_task_id_
        );
        info.reason = errors_.reason();
        info.error = errors_.details();
    });
    return info;
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
    ++generation_request_;
    const auto epoch = coordinator_.invalidate();
    media_.stop();
    render_.unbind(epoch);
    coordinator_.sync([this, reason] {
        errors_.reset(reason);
        if (!cleanup(false))
            throw Error(
                XMAX_API_ERROR,
                "Server session close failed",
                errors_.error() ? errors_.error()->api_code : 0,
                errors_.error() ? errors_.error()->http_status : 0
            );
    });
}

/**
 * Copies viewport trajectory samples and schedules asynchronous delivery, retaining only the latest pending
 * sample.
 *
 * @param[in] frame The sampled points and actual video layout.
 */
void XmaxRealtimeManager::submit_interaction(
    const xmax_interaction_frame& frame
) {
    auto points = InteractionCoordinateMapper::map(frame);
    if (points.empty())
        return;
    const auto epoch = coordinator_.epoch();
    std::lock_guard<std::mutex> lock(interaction_mutex_);
    require(
        !interaction_task_.empty() && coordinator_.state() == XMAX_GENERATING,
        XMAX_INVALID_STATE,
        "No generation is active"
    );
    render_.trajectory.update(points);
    if (generation_.interaction.submit({epoch, interaction_task_, std::move(points)}))
        coordinator_.enqueue([this] { drain_interaction(); });
}

/**
 * Schedules one trajectory sample fairly so continuous input cannot monopolize lifecycle execution.
 */
void XmaxRealtimeManager::drain_interaction() {
    auto sample = generation_.interaction.take();
    if (!sample)
        return;

    const auto epoch = coordinator_.epoch();
    const auto cancel = coordinator_.cancellation(epoch);
    try {
        if (coordinator_.state() == XMAX_GENERATING)
            generation_.send_interaction(
                stream_,
                *sample,
                epoch,
                cancel,
                [this, cancel] {
                    if (Clock::now() >= coordinator_.deadline())
                        keepalive(cancel);
                }
            );
    } catch (const Error& error) {
        if (error.code != XMAX_CANCELLED) {
            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.code = error.code;
            event.state = coordinator_.state();
            text_copy(
                event.message,
                sizeof(event.message),
                XmaxLogger::redact(error.what())
            );
            coordinator_.emit(
                event,
                XMAX_REASON_FAILURE,
                &error
            );
        }
    }

    coordinator_.enqueue([this] { drain_interaction(); });
}
} // namespace xmax::detail
