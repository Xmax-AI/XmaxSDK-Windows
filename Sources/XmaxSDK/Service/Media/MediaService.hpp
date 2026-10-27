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
     * Validates camera targets against the five X2.1 formats without substituting dimensions.
     *
     * @param[in] format The requested camera encoding dimensions and frame rate; capture may use other
     * device-supported dimensions.
     * @param[in] model The selected model capability snapshot.
     */
    static xmax_video_format camera_input(
        xmax_video_format format,
        xmax_model_info model = RealtimeModelDefinition::capabilities
    ) {
        const auto result = input(format);
        bool supported = false;
        for (uint32_t i = 0; i < model.format_count; ++i) {
            const auto& value = model.supported_formats[i];
            supported = supported || (value.width == result.width && value.height == result.height);
        }
        require(
            supported,
            XMAX_INVALID_ARGUMENT,
            "Camera target is not supported by the selected model"
        );
        return result;
    }

    /**
     * Selects the closest model canvas by aspect ratio while preserving local input dimensions.
     *
     * @param[in] format The local media dimensions and frame rate.
     * @param[in] model The selected model capability snapshot.
     *
     * @return The closest-aspect model canvas with the input frame rate retained.
     */
    static xmax_video_format generation(
        xmax_video_format format,
        xmax_model_info model = RealtimeModelDefinition::capabilities
    ) {
        validate_format(format);
        const double aspect = double(format.width) / format.height;
        double best_distance = std::numeric_limits<double>::infinity();
        auto result = model.supported_formats[0];

        for (uint32_t i = 0; i < model.format_count; ++i) {
            const auto& candidate = model.supported_formats[i];
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
