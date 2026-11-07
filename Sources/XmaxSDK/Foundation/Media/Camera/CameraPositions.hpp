//
//  CameraPositions.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/Camera/CameraDevice.hpp"

namespace xmax::detail {

/**
 * Adds enclosure positions reported by Windows to a Media Foundation device snapshot.
 *
 * @param[in,out] devices The camera snapshot; unavailable position metadata is left unspecified.
 */
void annotate_camera_positions(
    std::vector<CameraDevice>& devices
);

} // namespace xmax::detail
