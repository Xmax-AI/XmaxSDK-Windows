//
//  camera_device.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <string>

namespace xmax {

/**
 * Camera identifiers, names, and default-device flags copied into app-owned storage.
 */
struct camera_device {

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
    bool is_default;
};
} // namespace xmax
