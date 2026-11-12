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
    std::shared_ptr<QualityHistory> history,
    MicrophoneValidator microphone_validator,
    MicrophoneFactory microphone
)
    : api_(std::move(api)),
      connection_(api_),
      stream_(
          std::move(factory),
          std::move(history),
          heartbeat,
          api_ ? model_info().minimum_input_pixels : default_uplink_minimum_pixels
      ),
      media_(std::move(camera), std::move(microphone_validator), std::move(microphone)),
      generation_(media_.interaction),
      heartbeat_interval_(heartbeat),
      lifecycle_(
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
            if (state.connection_state == XMAX_GENERATING) {
                const auto epoch = lifecycle_.epoch();
                connection_.start_heartbeat(state.session_id, heartbeat_interval_, [this, epoch](Error error) {
                    errors_.forward(std::move(error), Scope::Connection,
                                    [this, epoch] { return lifecycle_.epoch() == epoch; });
                });
            }
            std::lock_guard<std::mutex> lock(interaction_mutex_);
            interaction_task_ = state.connection_state == XMAX_GENERATING ? state.task_id : "";
            if (state.connection_state != XMAX_GENERATING)
                render_.trajectory.clear();
        },
        [this] { return media_.stop_local_capture(); }
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
 * @param[in] processing_format The resolved processing format. When present, the controller must be idle.
 * @return The revision identifying this external source binding.
 */
uint64_t XmaxRealtimeManager::prepare(
    xmax_video_format format,
    bool has_audio,
    std::optional<xmax_video_format> processing_format,
    std::optional<xmax_encoding_config> encoding
) {
    uint64_t binding = 0;
    lifecycle_.run(
        OperationKind::Media,
        [this, format, has_audio, processing_format, encoding, &binding](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_IDLE || (!processing_format && lifecycle_.state() == XMAX_READY),
                XMAX_INVALID_STATE,
                "Stop existing local media before creating an external stream"
            );
            validate_format(format);
            MediaService::input(processing_format.value_or(format), model_info());
            if (encoding)
                EncodingController::validate(*encoding);
            token.failure_scope(Scope::All);
            errors_.reset();
            external_start_pending_ = processing_format.has_value();
            change_state(XMAX_PREPARING);
            if (processing_format) {
                stream_.mirror(false);
                render_.mirror(false, mirror_.remote_view != 0);
                mirror_.local_preview = mirror_.published_video = 0;
                stream_.volume(0);
            }
            bind_local();
            media_.prepare_external(
                format,
                media_epoch_.load(),
                has_audio,
                processing_format
            );
            token.ensure_current();
            if (encoding)
                stream_.encoding(*encoding);
            binding = media_epoch_;
            if (!processing_format)
                complete_state(XMAX_READY, token);
        }
    );
    return binding;
}

/**
 * Publishes readiness only after the application installs frame delivery on this binding.
 *
 * @param[in] binding The pending receiver revision.
 */
void XmaxRealtimeManager::complete_external_stream(
    uint64_t binding
) {
    lifecycle_.run(
        OperationKind::Media,
        [this, binding](const Lifecycle::Token& token) {
            require(
                external_start_pending_ && lifecycle_.state() == XMAX_PREPARING && requesting(binding),
                XMAX_INVALID_STATE,
                "The external stream is no longer pending startup"
            );
            complete_state(
                XMAX_READY,
                token,
                [this] { external_start_pending_ = false; }
            );
        }
    );
}

/**
 * Binds local preview to a new media revision, rejecting earlier frames.
 */
void XmaxRealtimeManager::bind_local(bool preserve_stream) {
    std::lock_guard<std::mutex> lock(media_binding_mutex_);
    if (!preserve_stream) {
        ++local_stream_id_;
        camera_preview_attached_ = false;
    }
    camera_preview_has_frame_ = false;
    const auto token = ++media_epoch_;
    failed_media_epoch_ = 0;
    render_.bind_local(token);
    media_.external().preview(
        [this, token, first_frame = true](Frame frame) mutable {
            render_.receive_local(std::move(frame), token);
            if (std::exchange(first_frame, false) && camera_preview_pending_) {
                lifecycle_.enqueue([this, token] {
                    if (media_epoch_ != token)
                        return;
                    camera_preview_has_frame_ = true;
                    publish_camera_ready();
                });
            }
        },
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
    external_start_pending_ = false;
    camera_preview_pending_ = false;
    ++local_stream_id_;
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
 * @param[in] binding The source revision, or zero to query the current input without revision matching.
 *
 * @return True for a live binding, including while disconnected; false after failure or unbinding.
 */
bool XmaxRealtimeManager::requesting(
    uint64_t binding
) const {
    std::lock_guard<std::mutex> lock(media_binding_mutex_);
    return (binding == 0 || media_epoch_ == binding) && failed_media_epoch_ != media_epoch_.load() &&
           !media_.camera_active() && media_.external().bound();
}

/**
 * Copies and submits external pixels on the calling media thread.
 *
 * @param[in] frame The borrowed pixels, valid until this synchronous call returns.
 * @param[in] binding The source revision, or zero to submit to the current input directly.
 *
 * @note Stale source revisions discard input. Invalid current source-bound frames fail that source;
 * invalid direct submissions report an error without terminating the input.
 */
void XmaxRealtimeManager::push(
    const xmax_video_frame& frame,
    uint64_t binding
) {
    if (binding != 0) {
        if (media_epoch_ != binding || failed_media_epoch_ == binding || media_.camera_active())
            return;
    } else {
        require(
            !media_.camera_active(),
            XMAX_INVALID_STATE,
            "External frames cannot replace SDK camera capture"
        );
    }

    try {
        media_.external().push(
            frame,
            binding
        );
    } catch (const Error& error) {
        if (binding == 0)
            throw;

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
    } catch (...) {
        if (binding == 0)
            throw;

        const Error source_error(
            XMAX_MEDIA_ERROR,
            "Could not process external video pixels"
        );
        external_failure(
            binding,
            source_error
        );
    }
}


/**
 * Releases local capture and input bindings after disconnection.
 */
void XmaxRealtimeManager::stop_media(StopMediaSource source) {
    lifecycle_.run(
        OperationKind::Media,
        [this, source](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_IDLE || lifecycle_.state() == XMAX_READY ||
                    ((external_start_pending_ || camera_preview_pending_) && lifecycle_.state() == XMAX_PREPARING),
                XMAX_INVALID_STATE,
                "Disconnect before stopping local media"
            );
            if ((source == StopMediaSource::Camera && !media_.camera_active()) ||
                (source == StopMediaSource::External && (media_.camera_active() || !media_.external().bound())))
                return;
            token.failure_scope(Scope::All);
            media_.close();
            unbind_local();
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
        can_connect(),
        XMAX_INVALID_STATE,
        "Prepare local media before connecting"
    );
    require(
        !api_->config().key.empty(),
        XMAX_INVALID_API_KEY,
        "An Xmax API Key is required"
    );
    errors_.reset();
    camera_preview_pending_ = false;
    change_state(XMAX_CONNECTING);

    const bool capture_microphone = media_.external().allows_microphone() &&
                                    (microphone || media_.microphone().enabled());
    if (capture_microphone) {
        prepare_microphone(epoch);
        require(!cancel(), XMAX_CANCELLED, "Connection cancelled");
    }

    timing_.begin_session();
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
    callbacks.video_unpublished = [this, epoch] {
        if (lifecycle_.epoch() == epoch)
            render_.clear_result(epoch);
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
    connection_.join(
        stream_,
        media_.external().format(),
        capture_microphone || media_.external().has_audio(),
        std::move(callbacks),
        cancel,
        MediaService::generation(media_.external().format(), model_info())
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
    if (capture_microphone)
        media_.microphone().activate();
    timing_.finish_join();
}

/**
 * Creates a server session and joins RTC without starting generation.
 *
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
uint64_t XmaxRealtimeManager::connect(
    bool microphone,
    uint64_t local_revision,
    Cancel caller_cancelled
) {
    require(!caller_cancelled || !caller_cancelled(), XMAX_CANCELLED, "Connection cancelled");
    uint64_t remote_revision = 0;
    lifecycle_.run(
        OperationKind::Connection,
        [this, microphone, local_revision, caller_cancelled = std::move(caller_cancelled),
         &remote_revision](const Lifecycle::Token& token) {
            const auto cancel = [operation_cancelled = token.cancellation(), caller_cancelled] {
                return operation_cancelled() || (caller_cancelled && caller_cancelled());
            };
            require(!cancel(), XMAX_CANCELLED, "Connection cancelled");
            require(!local_revision || local_revision == local_stream_revision(),
                    XMAX_INVALID_STATE, "The local media stream has been replaced");
            require(
                can_connect(),
                XMAX_INVALID_STATE,
                "Media is not ready or a connection already exists"
            );
            token.failure_scope(Scope::Connection);
            connect_impl(
                cancel,
                lifecycle_.epoch(),
                microphone
            );
            require(!cancel(), XMAX_CANCELLED, "Connection cancelled");
            remote_revision = lifecycle_.epoch() + 1;
            complete_state(
                XMAX_CONNECTED,
                token
            );
        }
    );
    return remote_revision;
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

xmax_encoding_config XmaxRealtimeManager::encoding_config() {
    xmax_encoding_config result{};
    lifecycle_.sync([this, &result] { result = stream_.encoding(); });
    return result;
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
        render_.unbind(lifecycle_.epoch());
        try {
            stream_.stop_generation([] { return false; });
        } catch (const std::exception& error) {
            XmaxLogger::write(LogCategory::Realtime, XMAX_LOG_ERROR, [&](auto language) {
                return XmaxLogger::localized(language, "停止实时生成失败", "Failed to stop realtime generation") +
                       "\n└─ " + error.what();
            });
        }
        generation_.reset(false);
        render_.trajectory.clear();
        return {connection_.active().id, media_.external().bound(), true};
    }
    const bool close = scope == Scope::All;
    media_.microphone().stop();

    generation_.reset(true);
    render_.trajectory.clear();
    media_.stop();
    {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        statistics_[0] = statistics_[1] = {};
    }
    render_.unbind(lifecycle_.epoch());

    timing_.reset_publication();

    const auto session_id = connection_.active().id;

    try {
        connection_.close(stream_);
    } catch (const std::exception& error) {
        XmaxLogger::write(LogCategory::Realtime, XMAX_LOG_ERROR, [&](auto language) {
            return XmaxLogger::localized(language, "断开实时连接失败", "Failed to disconnect realtime") +
                   "\n├─ session=" + session_id + "\n└─ " + error.what();
        });
    }

    if (close) {
        render_.close_beauty();
        media_.close();
        unbind_local();
    }

    return {session_id, media_.external().bound(), true};
}

/**
 * Cancels in-flight work and disconnects, optionally closing local media.
 *
 * @param[in] close Whether to also release local media and cached generation conditions.
 */
void XmaxRealtimeManager::disconnect(
    bool close
) {
    if (!close) {
        disconnect_reason(XMAX_REASON_NORMAL);
        return;
    }

    lifecycle_.terminate(Scope::All);
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
    xmax_video_format format,
    std::optional<xmax_encoding_config> encoding
) {
    lifecycle_.run(
        OperationKind::Media,
        [this, format, encoding](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_READY && media_.camera_active(),
                XMAX_INVALID_STATE,
                "Camera format can only be updated before connection"
            );

            const auto resolved = MediaService::camera_input(format, model_info());
            if (encoding)
                EncodingController::validate(*encoding);
            const auto current = media_.external().format();
            const auto current_encoding = stream_.encoding();
            if (resolved.width == current.width && resolved.height == current.height && resolved.fps == current.fps &&
                (!encoding || (encoding->minimum_bitrate_kbps == current_encoding.minimum_bitrate_kbps &&
                               encoding->maximum_bitrate_kbps == current_encoding.maximum_bitrate_kbps &&
                               encoding->preference == current_encoding.preference)))
                return;

            try {
                media_.update_camera_format(
                    resolved,
                    media_failure(media_epoch_),
                    token.cancellation()
                );
                token.ensure_current();
                if (encoding)
                    stream_.encoding(*encoding);
            } catch (const Error& error) {
                token.ensure_current();
                if (!media_.camera_active()) {
                    errors_.record(error);
                    unbind_local();
                    change_state(XMAX_IDLE);
                }
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
                cancel
            ));
        }
    );
}

/**
 * Returns the current state snapshot with its reason and error.
 */
xmax_event_info XmaxRealtimeManager::state_info() const {
    return lifecycle_.snapshot();
}

/**
 * Disconnects for the specified reason while retaining local media.
 *
 * @param[in] reason The final state reason.
 * @param[in] failure The SDK error when the caller supplies a failure reason.
 */
void XmaxRealtimeManager::disconnect_reason(
    xmax_realtime_reason reason,
    std::optional<Error> failure
) {
    require(
        reason == XMAX_REASON_NORMAL || reason == XMAX_REASON_ORIENTATION_CHANGED ||
            (reason == XMAX_REASON_FAILURE && failure.has_value()),
        XMAX_INVALID_ARGUMENT,
        "Invalid disconnect reason"
    );
    lifecycle_.disconnect(reason, std::move(failure));
}

/**
 * Copies viewport trajectory samples and schedules asynchronous delivery, retaining only the latest
 * pending sample.
 *
 * @param[in] frame The sampled points and actual video layout.
 */
void XmaxRealtimeManager::submit_interaction(
    const xmax_interaction_frame& frame,
    std::optional<bool> mirrored,
    uint64_t remote_revision
) {
    std::lock_guard<std::mutex> lock(interaction_mutex_);
    const auto epoch = lifecycle_.epoch();
    require(!remote_revision || remote_revision == epoch + 1,
            XMAX_INVALID_STATE, "The result stream has been replaced");
    require(
        !interaction_task_.empty() && lifecycle_.state() == XMAX_GENERATING,
        XMAX_INVALID_STATE,
        "No generation is active"
    );
    auto mapped_frame = frame;
    if (remote_revision) {
        const auto local = media_.external().format();
        mapped_frame.displayed_video_width = local.width;
        mapped_frame.displayed_video_height = local.height;
    }
    auto points = InteractionCoordinateMapper::map(mapped_frame);
    if (points.empty())
        return;
    render_.trajectory.update(points);
    if (mirrored.value_or(render_.remote_mirrored())) {
        for (auto& point : points)
            point.x = 1.0 - point.x;
    }
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
                cancel
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
        external_failure(binding, Error(XMAX_MEDIA_ERROR, error.what(), error.api_code, error.http_status));
    } catch (...) {
        external_failure(binding, Error(XMAX_MEDIA_ERROR, "Could not process external PCM samples"));
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

namespace xmax::detail {
void XmaxRealtimeManager::remote_video_beauty(std::optional<BeautyOptions> options) {
    if (options) validate_beauty(*options);
    lifecycle_.sync([this, options] { render_.beauty(options); });
}
void XmaxRealtimeManager::remote_video_beauty_error_listener(BeautyErrorListener listener) {
    BeautyErrorListener previous;
    lifecycle_.sync([this, &previous, listener = std::move(listener)]() mutable {
        previous = render_.beauty_listener(std::move(listener));
    });
}
}
