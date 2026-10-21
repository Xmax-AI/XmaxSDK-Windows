//
//  CameraDevice.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * 描述摄像头的稳定设备标识、显示名称及默认设备标记。
 */
struct CameraDevice {

    /**
     * 摄像头设备标识，用于后续明确选择该设备。
     */
    std::string id;

    /**
     * 用于界面显示的摄像头名称，不用于唯一匹配设备。
     */
    std::string name;

    /**
     * 设备是否为本次枚举结果中的默认摄像头。
     */
    bool is_default = false;
};
} // namespace xmax::detail
