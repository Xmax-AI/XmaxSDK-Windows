//
//  MediaService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include "Foundation/Models/RealtimeModelDefinition.hpp"
#include <cmath>
#include <limits>

namespace xmax::detail {

/**
 * 校验编码目标，并选择模型支持的生成画布；不处理视频像素。
 */
class MediaService {

  public:

    /**
     * 模型支持的五组画布尺寸，帧率默认值为 30。
     */
    inline static constexpr auto formats = RealtimeModelDefinition::formats;

    /**
     * 校验外部输入规格，保留接入方提供的视频比例。
     *
     * @param[in] format 输入尺寸和帧率，帧率范围为 [1, 60]。
     * @return 通过校验的原始输入规格。
     */
    static xmax_video_format input(
        xmax_video_format format
    ) {
        validate_format(format);
        return format;
    }

    /**
     * 校验模型支持的摄像头目标，将旧版尺寸映射到同方向的 1080p 规格。
     *
     * @param[in] format 摄像头请求的编码尺寸和帧率；实际采集帧可使用设备支持的其他尺寸。
     */
    static xmax_video_format camera_input(
        xmax_video_format format
    ) {
        auto result = input(format);
        bool supported = (format.width == 1920 && format.height == 1024) ||
                         (format.width == 1024 && format.height == 1920);

        for (const auto& value : formats)
            supported = supported || (value.width == format.width && value.height == format.height);

        require(
            supported, XMAX_INVALID_ARGUMENT,
            "Supported input sizes: 1920x1080, 1080x1920, 1408x1408, 1152x1536, 1536x1152; "
            "legacy 1920x1024 and 1024x1920 inputs are also accepted"
        );

        if (result.width == 1920 && result.height == 1024)
            result.height = 1080;
        else if (result.width == 1024 && result.height == 1920)
            result.width = 1080;
        return result;
    }

    /**
     * 按媒体比例选择最接近的模型画布，保留本地输入尺寸。
     *
     * @param[in] format 本地媒体的尺寸和帧率。
     * @return 宽高比最接近输入的模型画布尺寸，帧率沿用输入值。
     */
    static xmax_video_format generation(
        xmax_video_format format
    ) {
        validate_format(format);
        const double aspect = double(format.width) / format.height;
        double best_distance = std::numeric_limits<double>::infinity();
        auto result = formats.front();

        for (const auto& candidate : formats) {
            const double distance = std::abs(std::log(aspect / (double(candidate.width) / candidate.height)));
            if (distance < best_distance) {
                result = candidate;
                best_distance = distance;
            }
        }

        result.fps = format.fps;
        return result;
    }
};
} // namespace xmax::detail
