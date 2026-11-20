//
//  CameraStreamSettings.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//
#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <cmath>
#include <optional>

namespace xmax::detail {

struct CameraProcessingOptions {
    std::optional<xmax_video_format> capture_format;
    unsigned rotation = 0;
    double warmup_ms = 0;
    bool limit_output_rate = false;
};

struct CameraStreamSettings : CameraProcessingOptions {
    CameraStreamSettings() { limit_output_rate = true; }
    xmax_camera_position position = XMAX_CAMERA_POSITION_FRONT;
    std::optional<bool> mirror;
};

inline void validate_camera_processing(const CameraProcessingOptions& options) {
    require(options.rotation == 0 || options.rotation == 90 || options.rotation == 180 || options.rotation == 270,
        XMAX_INVALID_ARGUMENT, "Camera rotation must be 0, 90, 180, or 270 degrees");
    const auto maximum_ms = std::chrono::duration<double, std::milli>(Clock::duration::max() / 4).count();
    require(std::isfinite(options.warmup_ms) && options.warmup_ms >= 0 && options.warmup_ms <= maximum_ms,
        XMAX_INVALID_ARGUMENT, "Camera warmup must be finite, non-negative, and representable by the playback clock");
    if (options.capture_format) {
        const auto format = *options.capture_format;
        require(format.width > 0 && format.height > 0 && format.width <= 8192 && format.height <= 8192 &&
            format.fps > 0 && format.fps <= uint32_t(INT32_MAX), XMAX_INVALID_ARGUMENT, "Invalid camera capture format");
    }
}

} // namespace xmax::detail
