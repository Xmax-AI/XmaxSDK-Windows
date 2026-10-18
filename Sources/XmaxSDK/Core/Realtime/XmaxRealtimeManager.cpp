#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Service/Media/MediaService.hpp"
#include <algorithm>
#include <cstring>
namespace xmax::detail {
static void text_copy(char* out, size_t size, const std::string& text) {
    const auto count = std::min(size - 1, text.size());
    std::memcpy(out, text.data(), count);
    out[count] = 0;
}
XmaxRealtimeManager::XmaxRealtimeManager(std::shared_ptr<ApiService> api, RtcFactory factory,
                                         std::chrono::milliseconds heartbeat, CameraFactory camera,
                                         std::shared_ptr<QualityHistory> history)
    : api_(std::move(api)), service_(api_), stream_(std::move(factory), std::move(history)),
      media_(std::move(camera)), heartbeat_interval_(heartbeat),
      coordinator_([this] { this->heartbeat(); }, [this](Error error) { fail(std::move(error)); }) {
    require(bool(api_), XMAX_INVALID_ARGUMENT, "API service is required");
    require(heartbeat.count() > 0, XMAX_INVALID_ARGUMENT, "Invalid heartbeat interval");
}
XmaxRealtimeManager::~XmaxRealtimeManager() {
    try {
        disconnect(true);
    } catch (...) {
    }
    coordinator_.shutdown();
}
void XmaxRealtimeManager::change_state(xmax_state state) {
    coordinator_.state(state);
    xmax_event event{};
    event.type = XMAX_EVENT_STATE;
    event.state = state;
    text_copy(event.session_id, sizeof(event.session_id), service_.active().id);
    text_copy(event.task_id, sizeof(event.task_id), stream_.task());
    coordinator_.emit(event);
}
void XmaxRealtimeManager::prepare(xmax_video_format format) {
    coordinator_.sync([this, format] {
        require(coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
                "Disconnect before replacing an external source");
        const auto resolved = MediaService::input(format);
        bind_local();
        media_.prepare_external(resolved);
        camera_microphone_ = false;
        change_state(XMAX_READY);
    });
}
bool XmaxRealtimeManager::requesting() const {
    return !media_.camera_active() && input_epoch_.load() == coordinator_.epoch() &&
           media_.external().requesting();
}
void XmaxRealtimeManager::bind_local() {
    const auto token = ++media_epoch_;
    render_.bind_local(token);
    media_.external().preview([this, token](Frame frame) { render_.receive_local(std::move(frame), token); });
}
std::function<void(Error)> XmaxRealtimeManager::media_failure(uint64_t token) {
    return [this, token](Error error) {
        coordinator_.enqueue([this, token, error] {
            if (media_epoch_ != token || !media_.camera_active())
                return;
            coordinator_.invalidate();
            cleanup(true);
            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.code = error.code;
            event.state = coordinator_.state();
            text_copy(event.message, sizeof(event.message), error.what());
            coordinator_.emit(event);
        });
    };
}
void XmaxRealtimeManager::prepare_camera(std::string device, xmax_video_format format, bool microphone) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, device = std::move(device), format, microphone, epoch] {
        require(!coordinator_.cancellation(epoch)(), XMAX_CANCELLED, "Camera startup cancelled");
        require(coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
                "Disconnect before replacing local media");
        const auto resolved = MediaService::input(format);
        bind_local();
        try {
            media_.prepare_camera(device, resolved, media_failure(media_epoch_),
                                  coordinator_.cancellation(epoch));
            camera_microphone_ = microphone;
            stream_.volume(0);
            change_state(XMAX_READY);
        } catch (...) {
            media_.close();
            render_.unbind_local(++media_epoch_);
            change_state(XMAX_IDLE);
            throw;
        }
    });
}
void XmaxRealtimeManager::stop_media() {
    coordinator_.sync([this] {
        require(coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
                "Disconnect before stopping local media");
        media_.close();
        render_.unbind_local(++media_epoch_);
        camera_microphone_ = false;
        change_state(XMAX_IDLE);
    });
}
void XmaxRealtimeManager::push(const xmax_video_frame& frame) {
    require(!media_.camera_active(), XMAX_INVALID_STATE, "External frames cannot replace SDK camera capture");
    media_.external().push(frame);
}
void XmaxRealtimeManager::connect_impl(const Cancel& cancel, uint64_t epoch, bool microphone) {
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    require(coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE, "Prepare local media before connecting");
    require(!api_->config().key.empty(), XMAX_INVALID_ARGUMENT, "An Xmax API Key is required");
    stream_.allocate();
    launch_started_ = Clock::now();
    launch_timing_ = {};
    launch_timing_.type = XMAX_EVENT_TIMING;
    launch_timing_.session_ms = launch_timing_.rtc_join_ms = launch_timing_.publish_ms =
        launch_timing_.first_frame_ms = launch_timing_.total_ms = -1;
    change_state(XMAX_CONNECTING);
    const auto& session = service_.create(cancel);
    launch_timing_.session_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - launch_started_).count();
    const auto joining = Clock::now();
    RtcCallbacks callbacks;
    callbacks.frame = [this, epoch](Frame frame) {
        if (coordinator_.epoch() == epoch)
            render_.receive(std::move(frame), epoch);
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
                for (auto result : stream_.observe_quality(event.direction, event.quality, sampled,
                                                           coordinator_.cancellation(epoch)))
                    coordinator_.emit(result);
            });
        }
    };
    callbacks.failure = [this, epoch](Error error) {
        coordinator_.enqueue([this, epoch, error] {
            if (coordinator_.epoch() == epoch && !service_.active().id.empty())
                fail(error);
        });
    };
    stream_.join(session.room, media_.external().format(), microphone || camera_microphone_,
                 std::move(callbacks), cancel);
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    render_.bind(epoch);
    input_epoch_ = epoch;
    media_.external().start([this, cancel](const Frame& frame) {
        if (!cancel())
            stream_.push(frame);
    });
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    launch_timing_.rtc_join_ms = std::chrono::duration<double, std::milli>(Clock::now() - joining).count();
    change_state(XMAX_CONNECTED);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}
void XmaxRealtimeManager::connect(bool microphone) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, microphone, epoch] {
        require(coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
                "Media is not ready or a connection already exists");
        try {
            connect_impl(coordinator_.cancellation(epoch), epoch, microphone);
        } catch (...) {
            auto original = std::current_exception();
            coordinator_.invalidate();
            cleanup(false);
            std::rethrow_exception(original);
        }
    });
}
void XmaxRealtimeManager::generate_impl(const Context& context, const Cancel& cancel) {
    if (coordinator_.state() == XMAX_GENERATING) {
        stream_.update(context.prompt, context.reference, context.face, cancel);
        context_ = context;
        return;
    }
    require(coordinator_.state() == XMAX_CONNECTED, XMAX_INVALID_STATE, "No realtime connection is active");
    const auto generation = Clock::now();
    stream_.start(context.prompt, context.reference, context.face, cancel);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
    stream_.wait_remote(cancel, [this, cancel] {
        if (Clock::now() >= coordinator_.deadline())
            keepalive(cancel);
    });
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    launch_timing_.first_frame_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - generation).count();
    launch_timing_.total_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - launch_started_).count();
    coordinator_.emit(launch_timing_);
    change_state(XMAX_GENERATING);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
    context_ = context;
}
void XmaxRealtimeManager::start(std::string prompt, std::string reference, bool face, bool microphone) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync(
        [this, context = Context{std::move(prompt), std::move(reference), face}, microphone, epoch] {
            const auto cancel = coordinator_.cancellation(epoch);
            require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
            require(coordinator_.state() == XMAX_READY || coordinator_.state() == XMAX_CONNECTED ||
                        coordinator_.state() == XMAX_GENERATING,
                    XMAX_INVALID_STATE, "Prepare local media before generation");
            // Unsupported RTC and an empty key leave ready media unchanged.
            if (coordinator_.state() == XMAX_READY) {
                require(!api_->config().key.empty(), XMAX_INVALID_ARGUMENT, "An Xmax API Key is required");
            }
            if (coordinator_.state() == XMAX_GENERATING) {
                generate_impl(context, cancel);
                return;
            }
            try {
                if (coordinator_.state() == XMAX_READY)
                    connect_impl(cancel, epoch, microphone);
                generate_impl(context, cancel);
            } catch (...) {
                auto original = std::current_exception();
                coordinator_.invalidate();
                cleanup(false);
                std::rethrow_exception(original);
            }
        });
}
void XmaxRealtimeManager::generate(const xmax_generation_context* value) {
    std::optional<Context> incoming;
    if (value)
        incoming = Context{value->prompt, value->reference_path ? value->reference_path : "",
                           value->face_restoration != 0};
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, incoming = std::move(incoming), epoch] {
        require(incoming.has_value() || context_.has_value(), XMAX_INVALID_ARGUMENT,
                "Initial generation conditions are required");
        const auto context = incoming ? *incoming : *context_;
        require(coordinator_.state() == XMAX_READY || coordinator_.state() == XMAX_CONNECTED ||
                    coordinator_.state() == XMAX_GENERATING,
                XMAX_INVALID_STATE, "Prepare local media before generation");
        const auto cancel = coordinator_.cancellation(epoch);
        if (coordinator_.state() == XMAX_GENERATING) {
            generate_impl(context, cancel);
            return;
        }
        try {
            if (coordinator_.state() == XMAX_READY)
                connect_impl(cancel, epoch, camera_microphone_);
            generate_impl(context, cancel);
        } catch (...) {
            auto original = std::current_exception();
            coordinator_.invalidate();
            cleanup(false);
            std::rethrow_exception(original);
        }
    });
}
void XmaxRealtimeManager::update(std::string prompt, std::string reference, bool face) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, context = Context{std::move(prompt), std::move(reference), face}, epoch] {
        require(coordinator_.state() == XMAX_GENERATING, XMAX_INVALID_STATE, "No generation is active");
        generate_impl(context, coordinator_.cancellation(epoch));
    });
}
void XmaxRealtimeManager::switch_camera(std::string device) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, device = std::move(device), epoch] {
        const auto state = coordinator_.state();
        require(media_.camera_active() &&
                    (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
                XMAX_INVALID_STATE, "No switchable camera stream is active");
        const auto cancel = coordinator_.cancellation(epoch);
        try {
            media_.stop();
            if (state == XMAX_GENERATING) {
                stream_.stop_generation(cancel);
                change_state(XMAX_CONNECTED);
            }
            render_.unbind(epoch);
            bind_local();
            media_.switch_camera(device, media_failure(media_epoch_), cancel);
            if (state != XMAX_READY) {
                media_.external().start([this, cancel](const Frame& frame) {
                    if (!cancel())
                        stream_.push(frame);
                });
                render_.bind(epoch);
                if (state == XMAX_GENERATING)
                    generate_impl(*context_, cancel);
            }
        } catch (...) {
            auto original = std::current_exception();
            coordinator_.invalidate();
            cleanup(true);
            std::rethrow_exception(original);
        }
    });
}
xmax_event XmaxRealtimeManager::state_snapshot() {
    xmax_event event{};
    coordinator_.sync([this, &event] {
        event.struct_size = sizeof(event);
        event.type = XMAX_EVENT_STATE;
        event.state = coordinator_.state();
        text_copy(event.session_id, sizeof(event.session_id), service_.active().id);
        text_copy(event.task_id, sizeof(event.task_id), stream_.task());
    });
    return event;
}
xmax_video_format XmaxRealtimeManager::format() {
    xmax_video_format result{};
    coordinator_.sync([this, &result] { result = media_.external().format(); });
    return result;
}
float XmaxRealtimeManager::volume() {
    float result = 0;
    coordinator_.sync([this, &result] { result = stream_.volume(); });
    return result;
}
void XmaxRealtimeManager::interpolation(bool enabled) {
    require(!enabled, XMAX_UNSUPPORTED, "This build has no native frame interpolation backend");
}
void XmaxRealtimeManager::encoding(xmax_encoding_config value) {
    coordinator_.sync([this, value] { stream_.encoding(value); });
}
void XmaxRealtimeManager::adaptive(bool enabled) {
    coordinator_.sync([this, enabled] { stream_.adaptive(enabled); });
}
xmax_quality_status XmaxRealtimeManager::quality() {
    xmax_quality_status value{};
    coordinator_.sync([this, &value] { value = stream_.quality(); });
    return value;
}
bool XmaxRealtimeManager::cleanup(bool close) {
    media_.stop();
    render_.unbind(coordinator_.epoch());
    coordinator_.deadline(Clock::time_point::max());
    if (!service_.active().id.empty())
        change_state(XMAX_DISCONNECTING);
    stream_.stop();
    bool server_closed = true;
    const auto session_id = service_.active().id;
    try {
        service_.close();
    } catch (const std::exception&) {
        server_closed = false;
        xmax_event event{};
        event.type = XMAX_EVENT_ERROR;
        event.code = XMAX_API_ERROR;
        text_copy(event.session_id, sizeof(event.session_id), session_id);
        text_copy(event.message, sizeof(event.message),
                  "Server session close failed; verify the session status before restarting");
        coordinator_.emit(event);
    }
    if (close) {
        media_.close();
        render_.unbind_local(++media_epoch_);
        camera_microphone_ = false;
        context_.reset();
    }
    change_state(media_.external().bound() ? XMAX_READY : XMAX_IDLE);
    return server_closed;
}
void XmaxRealtimeManager::disconnect(bool close) {
    const auto epoch = coordinator_.invalidate();
    media_.stop();
    render_.unbind(epoch);
    coordinator_.sync([this, close] {
        require(cleanup(close), XMAX_API_ERROR,
                "Server session close failed; inspect the session ID in the error event");
    });
}
void XmaxRealtimeManager::fail(Error error) {
    coordinator_.invalidate();
    cleanup(false);
    xmax_event event{};
    event.type = XMAX_EVENT_ERROR;
    event.code = error.code;
    event.state = coordinator_.state();
    text_copy(event.message, sizeof(event.message), error.what());
    coordinator_.emit(event);
}
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
        fail(Error(XMAX_API_ERROR, "Session heartbeat failed"));
    }
}
void XmaxRealtimeManager::keepalive(const Cancel& cancel) {
    service_.heartbeat(cancel);
    stream_.heartbeat(cancel);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}
void XmaxRealtimeManager::volume(float value) {
    coordinator_.sync([this, value] { stream_.volume(value); });
}
} // namespace xmax::detail
