#include "Foundation/Media/Camera/CameraCapture.hpp"
namespace xmax::detail {
bool camera_available() noexcept {
    return false;
}
std::unique_ptr<CameraCapture> make_camera_capture() {
    throw Error(XMAX_UNSUPPORTED, "Native camera capture is unavailable on this platform");
}
} // namespace xmax::detail
