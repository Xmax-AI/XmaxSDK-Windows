//
//  WindowRenderer.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

/**
 * Reports whether this build supports native HWND rendering.
 */
bool native_window_available() noexcept;

/**
 * Renders an owned video frame with aspect fit or fill on the HWND owner thread.
 *
 * @param[in] source The frame or borrowed view to read, with a valid pixel layout.
 * @param[in] window The target Windows HWND as void*, owned by the calling thread.
 * @param[in] content_mode XMAX_CONTENT_FIT for aspect fit, or XMAX_CONTENT_FILL for centered aspect fill.
 */
void render_window(
    const Frame& source,
    void* window,
    int32_t content_mode
);

/**
 * Draws default trajectory lines and points over an HWND's video content.
 *
 * @param[in] window A Windows HWND owned by the calling thread.
 * @param[in] format The actual dimensions of the displayed video.
 * @param[in] content_mode The aspect-fit or aspect-fill layout mode.
 * @param[in] points The normalized trajectory in [0, 1].
 */
void render_trajectory(
    void* window,
    xmax_video_format format,
    int32_t content_mode,
    const std::vector<xmax_point>& points
);
} // namespace xmax::detail
