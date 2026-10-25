//
//  UnavailableCamera.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/Camera/CameraCapture.hpp"

namespace xmax::detail {

/**
 * Reports whether this SDK build provides native camera capture.
 */
bool camera_available() noexcept {
    return false;
}

/**
 * Creates a native camera adapter or reports unsupported capability on other platforms.
 *
 * @return An exclusively owned camera adapter; unsupported platforms throw a capability error.
 */
std::unique_ptr<CameraCapture> make_camera_capture() {
    throw Error(
        XMAX_UNSUPPORTED,
        "Native camera capture is unavailable on this platform"
    );
}
} // namespace xmax::detail
