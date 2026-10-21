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
 * 检查当前 SDK 构建是否提供原生摄像头采集后端。
 */
bool camera_available() noexcept {
    return false;
}

/**
 * 创建原生摄像头适配器，不支持的平台返回明确的能力错误。
 *
 * @return 独占拥有的原生摄像头适配器；平台不支持时抛出能力错误。
 */
std::unique_ptr<CameraCapture> make_camera_capture() {
    throw Error(XMAX_UNSUPPORTED, "Native camera capture is unavailable on this platform");
}
} // namespace xmax::detail
