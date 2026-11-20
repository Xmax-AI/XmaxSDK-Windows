//
//  CameraCaptureFormat.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//
#pragma once
#include "Foundation/Common.hpp"
#include <cmath>

namespace xmax::detail {

// Native capture constraints are preferences. Score dimensions and rate by relative distance,
// allowing a supported fallback when a driver cannot supply the requested capture mode exactly.
inline double camera_capture_distance(uint32_t width, uint32_t height, double fps, xmax_video_format preferred) {
    return std::abs(std::log(double(width) / preferred.width)) +
        std::abs(std::log(double(height) / preferred.height)) + std::abs(std::log(fps / preferred.fps));
}

} // namespace xmax::detail
