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
    resolution1536x1152 = XMAX_CAMERA_PRESET_1536_1152,

    /** A portrait 704 by 1280 X2.1 Flash frame. */
    resolution704x1280 = XMAX_CAMERA_PRESET_704_1280,

    /** A landscape 1280 by 704 X2.1 Flash frame. */
    resolution1280x704 = XMAX_CAMERA_PRESET_1280_704,

    /** A square 960 by 960 X2.1 Flash frame. */
    resolution960x960 = XMAX_CAMERA_PRESET_960_960,

    /** A landscape 1024 by 768 X2.1 Flash frame. */
    resolution1024x768 = XMAX_CAMERA_PRESET_1024_768,

    /** A portrait 768 by 1024 X2.1 Flash frame. */
    resolution768x1024 = XMAX_CAMERA_PRESET_768_1024
};
} // namespace xmax
