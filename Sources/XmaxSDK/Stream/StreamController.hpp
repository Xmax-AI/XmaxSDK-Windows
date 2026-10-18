#pragma once
#include "Stream/Quality/QualityController.hpp"
#include "Stream/Quality/QualityHistory.hpp"
#include "Stream/Room/RoomEvent.hpp"
#include "Stream/Room/RoomMessageSender.hpp"
namespace xmax::detail {
// Lifecycle methods are serialized by Core; push() is safe on a media thread.
class StreamController {
  public:
    explicit StreamController(RtcFactory factory = make_rtc, std::shared_ptr<QualityHistory> history = {})
        : factory_(std::move(factory)), history_(std::move(history)) {}
    ~StreamController();
    void allocate();
    void join(const Connection&, xmax_video_format, bool microphone, RtcCallbacks, const Cancel&);
    void start(const std::string& prompt, const std::string& reference, bool face, const Cancel&,
               const std::function<void()>& maintain = {});
    void wait_remote(const Cancel&, const std::function<void()>& keepalive);
    void update(const std::string& prompt, const std::string& reference, bool face, const Cancel&,
                const std::function<void()>& maintain = {});
    void heartbeat(const Cancel&);
    void stop() noexcept;
    void push(const Frame&);
    void volume(float);
    float volume() const { return volume_; }
    void stop_generation(const Cancel&);
    void encoding(xmax_encoding_config);
    void adaptive(bool enabled) {
        quality_.enabled = enabled;
        quality_.reset_samples();
    }
    xmax_quality_status quality() const;
    std::vector<xmax_event> observe_quality(int direction, int network_quality, double sampled_at,
                                            const Cancel&);
    const std::string& task() const { return task_; }

  private:
    RtcFactory factory_;
    std::shared_ptr<Rtc> rtc_, media_rtc_;
    RoomMessageSender sender_;
    Connection connection_;
    xmax_video_format format_{};
    std::string task_;
    float volume_ = 0;
    QualityController quality_;
    std::shared_ptr<QualityHistory> history_;
    Clock::time_point quality_saved_[2]{};
    xmax_encoding_config encoding_{sizeof(xmax_encoding_config), -1, 0, XMAX_ENCODER_MAINTAIN_FRAMERATE};
    VideoEncodingConfiguration uplink_encoding(int level) const;
    json generation_message(const std::string&, const std::string&, const std::string&, bool) const;
};
} // namespace xmax::detail
