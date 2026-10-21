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
 * 摄像头编码分辨率快捷选项，实际支持范围由模型决定。
 */
enum class RealtimeCameraPreset {

    /**
     * 没有摄像头流或没有匹配的预设。
     */
    none = XMAX_CAMERA_PRESET_NONE,

    /**
     * 16:9 横向画面。
     */
    resolution1920x1080 = XMAX_CAMERA_PRESET_1920_1080,

    /**
     * 9:16 竖向画面。
     */
    resolution1080x1920 = XMAX_CAMERA_PRESET_1080_1920,

    /**
     * 1:1 正方形画面。
     */
    resolution1408x1408 = XMAX_CAMERA_PRESET_1408_1408,

    /**
     * 3:4 竖向画面。
     */
    resolution1152x1536 = XMAX_CAMERA_PRESET_1152_1536,

    /**
     * 4:3 横向画面。
     */
    resolution1536x1152 = XMAX_CAMERA_PRESET_1536_1152
};
} // namespace xmax
