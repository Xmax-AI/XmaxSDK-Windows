//
//  MicrophoneDevices.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/Camera/CameraCapture.hpp"

namespace xmax::detail {

/** Lists recording endpoints and their default status without starting capture. */
std::vector<CameraDevice> list_microphone_devices();

/** Validates the selected Windows recording endpoint and checks capture access. */
void validate_microphone_device(
    const std::string& device
);

} // namespace xmax::detail
