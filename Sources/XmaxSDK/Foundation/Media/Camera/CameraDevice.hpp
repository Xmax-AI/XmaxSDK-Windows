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
 * A camera's stable device identifier, display name, and default-device flag.
 */
struct CameraDevice {

    /**
     * The camera identifier used to select this device explicitly.
     */
    std::string id;

    /**
     * The camera's display name; not a unique device identifier.
     */
    std::string name;

    /**
     * Whether this camera is the default in the current enumeration.
     */
    bool is_default = false;
};
} // namespace xmax::detail
