#pragma once
#include "Foundation/Media/VideoFrame.hpp"
namespace xmax::detail {
struct CameraDevice {
    std::string id, name;
    bool is_default = false;
};
struct CaptureCallbacks {
    std::function<void(Frame)> frame;
    std::function<void(Error)> failure;
};
class CameraCapture {
  public:
    virtual ~CameraCapture() = default;
    virtual std::vector<CameraDevice> devices() = 0;
    virtual void start(const std::string& device, xmax_video_format, CaptureCallbacks, const Cancel&) = 0;
    // After stop returns, no application-facing frame/failure callbacks may still run.
    virtual void stop() noexcept = 0;
};
using CameraFactory = std::function<std::unique_ptr<CameraCapture>()>;
std::unique_ptr<CameraCapture> make_camera_capture();
bool camera_available() noexcept;
} // namespace xmax::detail
