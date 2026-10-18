#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <mutex>
namespace xmax::detail {
using VideoFrameSink = std::function<void(const Frame&)>;
// Owns external input negotiation, pixel ownership and delivery. No RTC or UI dependency.
class ExternalVideoController {
  public:
    void prepare(xmax_video_format);
    void start(VideoFrameSink);
    void stop();
    void close();
    void push(const xmax_video_frame&);
    void push_owned(Frame);
    bool bound() const;
    bool requesting() const { return requesting_.load(); }
    xmax_video_format format() const;
    void preview(std::function<void(Frame)> sink);

  private:
    mutable std::mutex mutex_;
    xmax_video_format format_{};
    int64_t last_timestamp_ = -1;
    std::atomic<bool> requesting_{false};
    VideoFrameSink sink_;
    std::function<void(Frame)> preview_;
};
} // namespace xmax::detail
