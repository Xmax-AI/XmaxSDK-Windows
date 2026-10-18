#pragma once
#include "Foundation/Media/Camera/CameraCapture.hpp"
#include <condition_variable>
#include <mutex>
namespace xmax::detail {
// Serial lifecycle, SDK-normalized output and a real first-frame readiness barrier.
class CameraController {
  public:
    explicit CameraController(CameraFactory factory = make_camera_capture) : factory_(std::move(factory)) {}
    ~CameraController() { stop(); }
    std::vector<CameraDevice> devices() { return factory_()->devices(); }
    void start(const std::string&, xmax_video_format, CaptureCallbacks, const Cancel&);
    void stop() noexcept;

  private:
    CameraFactory factory_;
    std::unique_ptr<CameraCapture> capture_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool ready_ = false;
    std::string failure_;
    int64_t last_timestamp_ = -1;
};
} // namespace xmax::detail
