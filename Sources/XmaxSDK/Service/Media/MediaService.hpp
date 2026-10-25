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
#include "Service/Realtime/RealtimeModel.hpp"
#include <cmath>
#include <limits>

namespace xmax::detail {

/**
 * Validates encoding targets and selects a model-supported generation canvas without processing pixels.
 */
class MediaService {

  public:

    /**
     * The model's supported canvas sizes, with a default frame rate of 30.
     */
    inline static constexpr auto formats = RealtimeModelDefinition::formats;

    /**
     * Validates external input while preserving its aspect ratio.
     *
     * @param[in] format The input dimensions and frame rate, with frame rate in [1, 60].
     *
     * @return The validated original input format.
     */
    static xmax_video_format input(
        xmax_video_format format
    ) {
        validate_format(format);
        require(
            uint64_t(format.width) * format.height <= RealtimeModelDefinition::maximum_input_pixels,
            XMAX_INVALID_ARGUMENT,
            "Video input exceeds the model pixel limit"
        );
        require(
            format.width >= 4 && format.height >= 4,
            XMAX_INVALID_ARGUMENT,
            "External video dimensions must be at least four pixels"
        );
        return format;
    }

    /**
     * Validates camera targets, mapping 1080p compatibility presets to the supported format of the same orientation.
     *
     * @param[in] format The requested camera encoding dimensions and frame rate; capture may use other
     * device-supported dimensions.
     */
    static xmax_video_format camera_input(
        xmax_video_format format
    ) {
        auto result = input(format);
        if (result.width == 1920 && result.height == 1080)
            result.height = 1024;
        else if (result.width == 1080 && result.height == 1920)
            result.width = 1024;
        bool supported = false;
        for (const auto& value : formats)
            supported = supported || (value.width == result.width && value.height == result.height);
        require(
            supported,
            XMAX_INVALID_ARGUMENT,
            "Supported camera targets: 1024x1920 and 1920x1024"
        );
        return result;
    }

    /**
     * Selects the closest model canvas by aspect ratio while preserving local input dimensions.
     *
     * @param[in] format The local media dimensions and frame rate.
     *
     * @return The closest-aspect model canvas with the input frame rate retained.
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
