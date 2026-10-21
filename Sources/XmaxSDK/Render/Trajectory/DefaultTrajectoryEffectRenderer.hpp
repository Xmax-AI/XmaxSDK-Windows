//
//  DefaultTrajectoryEffectRenderer.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Render/WindowRenderer.hpp"

namespace xmax::detail {

/**
 * 在原生窗口的视频画面上绘制当前任务的默认轨迹效果。
 */
class DefaultTrajectoryEffectRenderer {

  public:

    /**
     * 绘制归一化轨迹，布局规则与当前视频画面一致。
     *
     * @param[in] window 当前线程拥有的 HWND。
     * @param[in] format 当前显示帧的实际尺寸。
     * @param[in] content_mode 等比例适应或居中填满。
     * @param[in] points 当前任务尚未过期的归一化采样。
     */
    void render(
        void* window,
        xmax_video_format format,
        int32_t content_mode,
        const std::vector<xmax_point>& points
    ) const {
        render_trajectory(window, format, content_mode, points);
    }
};
} // namespace xmax::detail
