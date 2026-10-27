//
//  RealtimeCameraPreset.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "xmax.h"

namespace xmax {

/**
 * Camera encoding-resolution presets; supported formats depend on the model.
 */
enum class RealtimeCameraPreset {

    /**
     * No camera stream or no matching preset.
     */
    none = XMAX_CAMERA_PRESET_NONE,

    /**
     * A landscape 16:9 X2.1 frame.
     */
    resolution1920x1080 = XMAX_CAMERA_PRESET_1920_1080,

    /**
     * A portrait 9:16 X2.1 frame.
     */
    resolution1080x1920 = XMAX_CAMERA_PRESET_1080_1920,

    /**
     * A square X2.1 frame.
     */
    resolution1408x1408 = XMAX_CAMERA_PRESET_1408_1408,

    /**
     * A portrait 3:4 X2.1 frame.
     */
    resolution1152x1536 = XMAX_CAMERA_PRESET_1152_1536,

    /**
     * A landscape 4:3 X2.1 frame.
     */
    resolution1536x1152 = XMAX_CAMERA_PRESET_1536_1152
};
} // namespace xmax
