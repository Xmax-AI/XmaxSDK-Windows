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
     * The 1080p landscape compatibility alias for the model's 1920x1024 target.
     */
    resolution1920x1080 = XMAX_CAMERA_PRESET_1920_1080,

    /**
     * The 1080p portrait compatibility alias for the model's 1024x1920 target.
     */
    resolution1080x1920 = XMAX_CAMERA_PRESET_1080_1920,

    /**
     * A reserved square preset; the current model rejects this value.
     */
    resolution1408x1408 = XMAX_CAMERA_PRESET_1408_1408,

    /**
     * A reserved 3:4 preset; the current model rejects this value.
     */
    resolution1152x1536 = XMAX_CAMERA_PRESET_1152_1536,

    /**
     * A reserved 4:3 preset; the current model rejects this value.
     */
    resolution1536x1152 = XMAX_CAMERA_PRESET_1536_1152,

    /**
     * The supported landscape camera target.
     */
    resolution1920x1024 = XMAX_CAMERA_PRESET_1920_1024,

    /**
     * The supported portrait camera target.
     */
    resolution1024x1920 = XMAX_CAMERA_PRESET_1024_1920
};
} // namespace xmax
