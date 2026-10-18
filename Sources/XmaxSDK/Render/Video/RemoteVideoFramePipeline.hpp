#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <mutex>
namespace xmax::detail {
// Owned CPU frames, one latest output, and generation tokens to reject stale RTC callbacks.
class RemoteVideoFramePipeline {
  public:
    void activate(uint64_t token);
    void deactivate(uint64_t token);
    void submit(Frame, uint64_t token);
    std::shared_ptr<Frame> acquire();

  private:
    std::mutex mutex_;
    uint64_t token_ = 0;
    bool active_ = false;
    std::shared_ptr<Frame> latest_;
};
} // namespace xmax::detail
