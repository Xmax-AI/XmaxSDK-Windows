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
 * Draws the current task's default trajectory over native-window video.
 */
class DefaultTrajectoryEffectRenderer {

  public:

    /**
     * Draws normalized trajectories using the displayed video's layout.
     *
     * @param[in] window An HWND owned by the calling thread.
     * @param[in] format The displayed frame's actual dimensions.
     * @param[in] content_mode Aspect fit or centered aspect fill.
     * @param[in] strokes The current task's fading normalized strokes.
     * @param[in] elapsed The gesture animation age in seconds.
     */
    void render(
        void* window,
        xmax_video_format format,
        int32_t content_mode,
        const std::vector<TrajectoryStroke>& strokes,
        double elapsed
    ) const {
        render_trajectory_effects(
            window,
            format,
            content_mode,
            strokes,
            elapsed
        );
    }
};
} // namespace xmax::detail
