//
//  StreamController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/StreamController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <cmath>

namespace xmax::detail {

/**
 * Leaves RTC, drains callbacks, and releases publication and signaling resources.
 */
StreamController::~StreamController() {
    stop();
}

/**
 * Creates the native RTC adapter before allocating a server session.
 */
void StreamController::allocate() {
    room_.allocate();
}

/**
 * Joins the room with input encoding, audio policy, and lifecycle-protected callbacks.
 *
 * @param[in] connection The current RTC room connection information.
 * @param[in] format The requested pixel dimensions and frame rate, subject to the interface's format
 * limits.
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 * @param[in] callbacks Callbacks for result frames, RTC events, failures, and first publication; drained
 * before stop returns.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] generation_format The generation canvas; zero dimensions select the input format.
 */
void StreamController::join(
    const Connection& connection,
    xmax_video_format format,
    bool microphone,
    RtcCallbacks callbacks,
    const Cancel& cancel,
    xmax_video_format generation_format
) {
    format_ = format;
    generation_format_ = generation_format.width ? generation_format : format;

    if (!quality_.enabled) {
        quality_.up.level = quality_.down.level = 1;
    } else if (history_) {
        quality_.up.level = history_->read(false);
        quality_.down.level = history_->read(true);
    } else {
        quality_.up.level =
            Clock::now() - quality_saved_[0] >= std::chrono::hours(1) ? 2 : cached_quality_[0];
        quality_.down.level =
            Clock::now() - quality_saved_[1] >= std::chrono::hours(1) ? 2 : cached_quality_[1];
    }

    while (quality_.up.level > 1) {
        try {
            uplink_encoding(quality_.up.level);
            break;
        } catch (const Error&) {
            --quality_.up.level;
        }
    }
    quality_.reset_samples();
    // Set playback policy before joining so result-user subscription starts at the correct volume.
    require(
        room_.backend() != nullptr,
        XMAX_INVALID_STATE,
        "Allocate an RTC backend before joining"
    );
    room_.backend()->volume(volume_);
    room_.backend()->external_audio(external_audio_);
    room_.join(
        connection,
        uplink_encoding(quality_.up.level),
        microphone,
        std::move(callbacks),
        cancel
    );
    room_.backend()->observe_remote_media(remote_media_enabled_);
    XmaxLogger::write(
        LogCategory::Stream,
        XMAX_LOG_INFO,
        [&](auto language) {
            return XmaxLogger::localized(
                       language,
                       "RTC 已连接 (RTC Connected)",
                       "RTC Connected"
                   ) +
                   "\n└─ " +
                   std::to_string(format.width) + " × " + std::to_string(format.height) + " @ " +
                   std::to_string(format.fps) + " fps, microphone=" + (microphone ? "on" : "off");
        }
    );
}

/**
 * Creates a task and sends start signaling, performing maintenance while waiting.
 *
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] maintain The maintenance callback invoked while waiting or throttled; empty omits
 * maintenance.
 */
void StreamController::start(
    const std::string& prompt,
    const std::string& reference,
    bool face,
    const Cancel& cancel,
    const std::function<void()>& maintain
) {
    room_.task = task_id();
    require(
        room_.joined(),
        XMAX_INVALID_STATE,
        "No RTC room is connected"
    );
    room_.backend()->begin_generation();
    room_.send(
        generation_message(
            "start",
            prompt,
            reference,
            face
        ),
        cancel,
        maintain
    );
}

/**
 * Waits for the first real result frame, checking cancellation and maintaining keepalives.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] keepalive The periodic keepalive callback; it must not hold a cross-thread lifecycle lock.
 */
void StreamController::wait_remote(
    const Cancel& cancel,
    const std::function<void()>& keepalive
) {
    room_.backend()->wait_remote(
        cancel,
        keepalive
    );
}

/**
 * Sends updated generation conditions using the current task identifier.
 *
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] maintain The maintenance callback invoked while waiting or throttled; empty omits
 * maintenance.
 */
void StreamController::update(
    const std::string& prompt,
    const std::string& reference,
    bool face,
    const Cancel& cancel,
    const std::function<void()>& maintain
) {
    require(
        room_.joined() && !room_.task.empty(),
        XMAX_INVALID_STATE,
        "No generation is active"
    );
    room_.send(
        generation_message(
            "change_condition",
            prompt,
            reference,
            face
        ),
        cancel,
        maintain
    );
}

/**
 * Sends RTC room keepalive signaling for the current task.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void StreamController::heartbeat(
    const Cancel& cancel
) {
    require(
        room_.joined(),
        XMAX_INVALID_STATE,
        "No RTC room is connected"
    );
    room_.send(
        room_event(
            "heartbeat",
            room_.connection
        ),
        cancel
    );
}

/**
 * Stops generation while retaining the RTC connection.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void StreamController::stop_generation(
    const Cancel& cancel
) {
    require(
        room_.joined(),
        XMAX_INVALID_STATE,
        "No RTC room is connected"
    );
    if (!room_.task.empty())
        room_.send(
            room_event(
                "stop",
                room_.connection,
                room_.task
            ),
            cancel
        );
    room_.task.clear();
}

/**
 * Disconnects media-thread RTC access, leaves the room, and clears signaling state.
 */
void StreamController::stop() noexcept {
    if (room_.joined() && !room_.task.empty()) {
        try {
            room_.send(
                room_event(
                    "stop",
                    room_.connection,
                    room_.task
                ),
                [] { return false; }
            );
        } catch (...) {
        }
    }
    room_.leave();
    quality_.reset_samples();
}

/**
 * Publishes a frame using a thread-safe snapshot of the active RTC adapter.
 *
 * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
 * synchronous call returns.
 */
void StreamController::push(
    const Frame& frame
) {
    room_.push(frame);
}

/**
 * Sets and retains the remote playback volume for the current or next connection.
 *
 * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
 */
void StreamController::volume(
    float value
) {
    require(
        std::isfinite(value) && value >= 0 && value <= 1,
        XMAX_INVALID_ARGUMENT,
        "Volume must be between 0 and 1"
    );
    if (room_.backend())
        room_.backend()->volume(value);
    volume_ = value;
}

/**
 * Resolves native encoding parameters from the quality level and bitrate overrides.
 *
 * @param[in] level The quality level, in [1, 5].
 */
VideoEncodingConfiguration StreamController::uplink_encoding(
    int level
) const {
    return encoding_.resolve(
        format_,
        level
    );
}

/**
 * Validates and applies uplink bitrate limits and degradation preference.
 *
 * @param[in] value The uplink bitrate limits and degradation preference; see xmax_encoding_config for
 * units and special values.
 */
void StreamController::encoding(
    xmax_encoding_config value
) {
    encoding_.configure(
        value,
        format_,
        quality_.up.level,
        room_.joined() ? room_.backend() : nullptr
    );
}

/**
 * Returns the current uplink and downlink quality levels and effective video formats.
 *
 * @return The adaptation setting, directional quality levels, and effective dimensions and frame rates.
 */
xmax_quality_status StreamController::quality() const {
    xmax_quality_status value{};
    value.struct_size = sizeof(value);
    value.adaptive_enabled = quality_.enabled;
    value.uplink_level = room_.joined() ? quality_.up.level : 0;
    value.downlink_level = room_.joined() ? quality_.down.level : 0;

    if (value.uplink_level) {
        value.uplink = quality_format(
            format_,
            quality_.up.level,
            false
        );
        value.downlink = quality_format(
            format_,
            quality_.down.level,
            true
        );
    }

    return value;
}

/**
 * Builds signaling from the active room, task identifier, and generation conditions.
 *
 * @param[in] event The room signaling event name.
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference An optional generation reference image path or URL.
 * @param[in] face Whether to enable face restoration for generation.
 *
 * @return The generation event JSON ready for room signaling.
 */
json StreamController::generation_message(
    const std::string& event,
    const std::string& prompt,
    const std::string& reference,
    bool face
) const {
    auto value =
        generation_event(
            event,
            room_.connection,
            room_.task,
            generation_format_,
            prompt,
            reference,
            face
        );
    if (uint64_t(format_.width) * generation_format_.height == uint64_t(format_.height) * generation_format_.width)
        value["params"].erase("padding");
    const auto target = quality_format(
        format_,
        quality_.down.level,
        true
    );
    value["params"]["target_size"] = json::array({target.width, target.height});

    if (event == "start")
        value["params"]["max_bitrate"] = std::min(10000, bitrate(target).second);

    return value;
}

/**
 * Updates directional network-quality policy and returns resulting quality or performance events.
 *
 * @param[in] direction The transport direction: 0 for uplink or 1 for downlink.
 * @param[in] network_quality The native network-quality value for this direction.
 * @param[in] sampled_at The sample's monotonic time in seconds; expired or duplicate samples are
 * ignored.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 *
 * @return The events triggered by this observation, or an empty collection if unchanged.
 */
std::vector<xmax_event> StreamController::observe_quality(
    int direction,
    int network_quality,
    double sampled_at,
    const Cancel& cancel
) {
    if (!quality_.enabled || !room_.joined() || room_.task.empty() || direction < 0 || direction > 1)
        return {};

    auto& policy = direction ? quality_.down : quality_.up;
    const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    auto proposal = policy.observe(
        network_quality,
        sampled_at,
        now,
        [this, direction, &policy](int candidate) {
            const auto current = quality_format(
                format_,
                policy.level,
                direction != 0
            );
            const auto next = quality_format(
                format_,
                candidate,
                direction != 0
            );
            if (current.width == next.width && current.height == next.height && current.fps == next.fps)
                return false;
            try {
                if (!direction)
                    uplink_encoding(candidate);
                return true;
            } catch (const Error&) {
                return false;
            }
        }
    );

    if (!proposal)
        return {};

    try {
        return {set_quality(
            direction,
            *proposal,
            cancel
        )};
    } catch (const Error& error) {
        policy.complete(
            *proposal,
            false,
            now
        );
        const auto previous = quality_format(
            format_,
            policy.level,
            direction != 0
        );
        xmax_event event{};
        event.type = XMAX_EVENT_QUALITY_CHANGE;
        event.code = error.code;
        event.direction = direction;
        event.quality = policy.level;
        event.width = int(previous.width);
        event.height = int(previous.height);
        event.fps = int(previous.fps);
        return {event};
    }
}

/**
 * Applies your app's directional level, preserving the previous level on failure.
 *
 * @param[in] direction 0 for uplink; 1 for downlink.
 * @param[in] level The target quality level, in [1, 5].
 * @param[in] cancel The cooperative cancellation predicate.
 * @param[in] maintain The keepalive callback invoked while waiting to send signaling.
 *
 * @return The committed quality-change event.
 */
xmax_event StreamController::set_quality(
    int direction,
    int level,
    const Cancel& cancel,
    const std::function<void()>& maintain
) {
    require(
        (direction == 0 || direction == 1) && level >= 1 && level <= 5,
        XMAX_INVALID_ARGUMENT,
        "Invalid quality direction or level"
    );
    require(
        room_.joined() && !room_.task.empty(),
        XMAX_INVALID_STATE,
        "No generation is active"
    );
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Quality adjustment cancelled"
    );

    auto& policy = direction ? quality_.down : quality_.up;
    const auto next = quality_format(
        format_,
        level,
        direction != 0
    );

    if (level != policy.level) {
        if (direction) {
            auto message = room_event(
                "change_target_size",
                room_.connection,
                room_.task
            );
            message["params"] = {
                {"target_size", json::array({next.width, next.height})},
                {"min_bitrate", 100},
                {"max_bitrate", std::min(10000, bitrate(next).second)}
            };
            room_.send(
                message,
                cancel,
                maintain
            );
        } else {
            room_.backend()->configure_encoding(uplink_encoding(level));
        }

        policy.complete(
            level,
            true,
            std::chrono::duration<double>(Clock::now().time_since_epoch()).count()
        );
        if (quality_.enabled) {
            quality_saved_[direction] = Clock::now();
            cached_quality_[direction] = level;
            if (history_)
                history_->write(
                    level,
                    direction != 0
                );
        }
    }

    xmax_event event{};
    event.type = XMAX_EVENT_QUALITY_CHANGE;
    event.direction = direction;
    event.quality = level;
    event.width = int(next.width);
    event.height = int(next.height);
    event.fps = int(next.fps);
    event.bitrate_kbps = event.packet_loss_percent = event.e2e_ms = -1;
    return event;
}

} // namespace xmax::detail
