#include "Stream/StreamController.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <cmath>
namespace xmax::detail {
StreamController::~StreamController() {
    stop();
}
void StreamController::allocate() {
    require(!rtc_, XMAX_INVALID_STATE, "An RTC backend is already allocated");
    // Fail before creating a billed server session when no backend is built in.
    rtc_ = factory_();
    require(bool(rtc_), XMAX_UNSUPPORTED, "No RTC backend is available");
}
void StreamController::join(const Connection& connection, xmax_video_format format, bool microphone,
                            RtcCallbacks callbacks, const Cancel& cancel) {
    require(bool(rtc_), XMAX_INVALID_STATE, "Allocate an RTC backend before joining");
    connection_ = connection;
    format_ = format;
    if (history_) {
        quality_.up.level = history_->read(false);
        quality_.down.level = history_->read(true);
    } else {
        if (Clock::now() - quality_saved_[0] >= std::chrono::hours(1))
            quality_.up.level = 2;
        if (Clock::now() - quality_saved_[1] >= std::chrono::hours(1))
            quality_.down.level = 2;
    }
    quality_.reset_samples();
    rtc_->join(connection, uplink_encoding(quality_.up.level), microphone, std::move(callbacks), cancel);
    rtc_->volume(volume_);
    std::atomic_store(&media_rtc_, rtc_);
}
void StreamController::start(const std::string& prompt, const std::string& reference, bool face,
                             const Cancel& cancel, const std::function<void()>& maintain) {
    task_ = task_id();
    rtc_->begin_generation();
    sender_.send(*rtc_, generation_message("start", prompt, reference, face), cancel, maintain);
}
void StreamController::wait_remote(const Cancel& cancel, const std::function<void()>& keepalive) {
    rtc_->wait_remote(cancel, keepalive);
}
void StreamController::update(const std::string& prompt, const std::string& reference, bool face,
                              const Cancel& cancel, const std::function<void()>& maintain) {
    require(rtc_ && !task_.empty(), XMAX_INVALID_STATE, "No generation is active");
    sender_.send(*rtc_, generation_message("change_condition", prompt, reference, face), cancel, maintain);
}
void StreamController::heartbeat(const Cancel& cancel) {
    require(bool(rtc_), XMAX_INVALID_STATE, "No RTC room is connected");
    sender_.send(*rtc_, room_event("heartbeat", connection_), cancel);
}
void StreamController::stop_generation(const Cancel& cancel) {
    require(bool(rtc_), XMAX_INVALID_STATE, "No RTC room is connected");
    if (!task_.empty())
        sender_.send(*rtc_, room_event("stop", connection_, task_), cancel);
    task_.clear();
}
void StreamController::stop() noexcept {
    std::atomic_store(&media_rtc_, std::shared_ptr<Rtc>{});
    if (rtc_) {
        if (!task_.empty()) {
            try {
                sender_.send(*rtc_, room_event("stop", connection_, task_), [] { return false; });
            } catch (...) {
            }
        }
        rtc_->leave();
        rtc_.reset();
    }
    connection_ = {};
    task_.clear();
    sender_.reset();
    quality_.reset_samples();
}
void StreamController::push(const Frame& frame) {
    const auto rtc = std::atomic_load(&media_rtc_);
    if (rtc)
        rtc->push(frame);
}
void StreamController::volume(float value) {
    require(std::isfinite(value) && value >= 0 && value <= 1, XMAX_INVALID_ARGUMENT,
            "Volume must be between 0 and 1");
    if (rtc_)
        rtc_->volume(value);
    volume_ = value;
}
VideoEncodingConfiguration StreamController::uplink_encoding(int level) const {
    const auto format = quality_format(format_, level, false);
    const auto budget = bitrate(format);
    const auto minimum = encoding_.minimum_bitrate_kbps < 0 ? budget.first : encoding_.minimum_bitrate_kbps;
    const auto maximum = encoding_.maximum_bitrate_kbps == 0 ? std::max(budget.second, minimum)
                                                             : encoding_.maximum_bitrate_kbps;
    return {format, minimum, maximum, encoding_.preference};
}
void StreamController::encoding(xmax_encoding_config value) {
    require(value.minimum_bitrate_kbps >= -1 && value.maximum_bitrate_kbps >= 0 &&
                value.maximum_bitrate_kbps <= 100000 && value.minimum_bitrate_kbps <= 100000 &&
                (value.maximum_bitrate_kbps == 0 ||
                 (value.minimum_bitrate_kbps < 0 ? 100 : value.minimum_bitrate_kbps) <=
                     value.maximum_bitrate_kbps) &&
                value.preference >= XMAX_ENCODER_AUTO && value.preference <= XMAX_ENCODER_MAINTAIN_QUALITY,
            XMAX_INVALID_ARGUMENT, "Invalid video encoding configuration");
    const auto old = encoding_;
    encoding_ = value;
    try {
        if (rtc_ && !connection_.room.empty())
            rtc_->configure_encoding(uplink_encoding(quality_.up.level));
    } catch (...) {
        encoding_ = old;
        throw;
    }
}
xmax_quality_status StreamController::quality() const {
    xmax_quality_status value{};
    value.struct_size = sizeof(value);
    value.adaptive_enabled = quality_.enabled;
    value.uplink_level = quality_.up.level;
    value.downlink_level = quality_.down.level;
    if (format_.width) {
        value.uplink = quality_format(format_, quality_.up.level, false);
        value.downlink = quality_format(format_, quality_.down.level, true);
    }
    return value;
}
json StreamController::generation_message(const std::string& event, const std::string& prompt,
                                          const std::string& reference, bool face) const {
    auto value = generation_event(event, connection_, task_, format_, prompt, reference, face);
    const auto target = quality_format(format_, quality_.down.level, true);
    value["params"]["target_size"] = json::array({target.width, target.height});
    if (event == "start")
        value["params"]["max_bitrate"] = std::min(10000, bitrate(target).second);
    return value;
}
std::vector<xmax_event> StreamController::observe_quality(int direction, int network_quality,
                                                          double sampled_at, const Cancel& cancel) {
    if (!quality_.enabled || !rtc_ || task_.empty() || direction < 0 || direction > 1)
        return {};
    auto& policy = direction ? quality_.down : quality_.up;
    const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    auto proposal = policy.observe(network_quality, sampled_at, now);
    if (!proposal)
        return {};
    const auto old_format = quality_format(format_, policy.level, direction != 0);
    const auto next_format = quality_format(format_, *proposal, direction != 0);
    if (old_format.width == next_format.width && old_format.height == next_format.height &&
        old_format.fps == next_format.fps)
        return {};
    xmax_event event{};
    event.type = XMAX_EVENT_QUALITY_CHANGE;
    event.direction = direction;
    event.quality = *proposal;
    event.width = int(next_format.width);
    event.height = int(next_format.height);
    event.fps = int(next_format.fps);
    event.bitrate_kbps = event.packet_loss_percent = event.e2e_ms = -1;
    try {
        require(!cancel(), XMAX_CANCELLED, "Quality adjustment cancelled");
        if (direction) {
            auto message = room_event("change_target_size", connection_, task_);
            message["params"] = {{"target_size", json::array({next_format.width, next_format.height})},
                                 {"min_bitrate", 100},
                                 {"max_bitrate", std::min(10000, bitrate(next_format).second)}};
            sender_.send(*rtc_, message, cancel);
        } else
            rtc_->configure_encoding(uplink_encoding(*proposal));
        policy.complete(*proposal, true, now);
        quality_saved_[direction] = Clock::now();
        if (history_)
            history_->write(policy.level, direction != 0);
    } catch (const Error& error) {
        policy.complete(*proposal, false, now);
        event.code = error.code;
        event.quality = policy.level;
        event.width = int(old_format.width);
        event.height = int(old_format.height);
        event.fps = int(old_format.fps);
    }
    return {event};
}
} // namespace xmax::detail
