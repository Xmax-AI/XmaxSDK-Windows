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
 * 检查当前构建是否支持 HWND 原生窗口呈现。
 */
bool native_window_available() noexcept;

/**
 * 在 HWND 所属线程按适应或填满模式呈现拥有像素的视频帧。
 *
 * @param[in] source 待读取的视频帧或借用视图，需包含有效的像素布局。
 * @param[in] window 目标 Windows HWND 转换为 void* 的值，必须由当前线程拥有。
 * @param[in] content_mode XMAX_CONTENT_FIT 等比例适应，或 XMAX_CONTENT_FILL 居中填满。
 */
void render_window(
    const Frame& source,
    void* window,
    int32_t content_mode
);

/**
 * 在已经绘制视频的 HWND 上叠加默认轨迹线和采样点。
 *
 * @param[in] window 当前线程拥有的 Windows HWND。
 * @param[in] format 当前显示视频的实际尺寸。
 * @param[in] content_mode 画面适应或填满方式。
 * @param[in] points [0, 1] 范围的归一化轨迹。
 */
void render_trajectory(
    void* window,
    xmax_video_format format,
    int32_t content_mode,
    const std::vector<xmax_point>& points
);
} // namespace xmax::detail
