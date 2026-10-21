//
//  InteractionCoordinateMapper.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <algorithm>
#include <cmath>

namespace xmax::detail {

/**
 * 将视口采样点映射到实际显示视频的归一化坐标，过滤适应模式的黑边。
 */
class InteractionCoordinateMapper {

  public:

    /**
     * 将采样映射到实际显示的视频区域，适应模式黑边不产生交互。
     *
     * @param[in] frame 视口尺寸、实际显示视频尺寸和采样点。
     * @return 按采样顺序保留的有效归一化点集。
     */
    static std::vector<xmax_point> map(
        const xmax_interaction_frame& frame
    ) {
        require(
            frame.points && frame.point_count > 0 && frame.point_count <= 256 &&
                std::isfinite(frame.viewport_width) && std::isfinite(frame.viewport_height) &&
                frame.viewport_width > 0 && frame.viewport_height > 0 && frame.displayed_video_width > 0 &&
                frame.displayed_video_height > 0 &&
                (frame.content_mode == XMAX_CONTENT_FIT || frame.content_mode == XMAX_CONTENT_FILL),
            XMAX_INVALID_ARGUMENT, "Invalid interaction viewport or point range"
        );
        const auto sx = frame.viewport_width / frame.displayed_video_width;
        const auto sy = frame.viewport_height / frame.displayed_video_height;
        const auto scale = frame.content_mode == XMAX_CONTENT_FIT ? std::min(sx, sy) : std::max(sx, sy);
        const auto width = frame.displayed_video_width * scale;
        const auto height = frame.displayed_video_height * scale;
        require(
            std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0, XMAX_INVALID_ARGUMENT,
            "Interaction viewport exceeds the supported range"
        );
        const auto left = (frame.viewport_width - width) / 2;
        const auto top = (frame.viewport_height - height) / 2;
        std::vector<xmax_point> points;
        points.reserve(frame.point_count);
        for (size_t i = 0; i < frame.point_count; ++i) {
            const auto& point = frame.points[i];
            require(
                std::isfinite(point.x) && std::isfinite(point.y), XMAX_INVALID_ARGUMENT,
                "Interaction points must be finite"
            );
            const auto x = (point.x - left) / width;
            const auto y = (point.y - top) / height;
            if (frame.content_mode == XMAX_CONTENT_FIT && (x < 0 || x > 1 || y < 0 || y > 1))
                continue;
            points.push_back({std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
        }
        return points;
    }
};
} // namespace xmax::detail
