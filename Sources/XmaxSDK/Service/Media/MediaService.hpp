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
     * @param[in] format The input dimensions and positive nominal frame rate.
     * @param[in] model The selected model capability snapshot.
     *
     * @return The validated original input format.
     */
    static xmax_video_format input(
        xmax_video_format format,
        const xmax_model_info& model = RealtimeModelDefinition::default_capabilities
    ) {
        validate_format(format);
        require(
            uint64_t(format.width) * format.height <= model.maximum_input_pixels,
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
     * Resolves a requested external processing format to the model area limits and four-pixel alignment.
     */
    static xmax_video_format external_input(
        xmax_video_format requested,
        const xmax_model_info& model
    );

    /** Resolves the downlink dimensions within the tier bounds. */
    static xmax_video_format target(
        xmax_video_format source,
        double maximum_width,
        double maximum_height
    );

    /** Resolves input geometry independently of frame rate and buffer layout. */
    static xmax_media_size media_size(xmax_media_size source, bool camera, const xmax_model_info& model);
    static xmax_media_size generation_size(xmax_media_size source, const xmax_model_info& model);
    static xmax_media_size target_size(xmax_media_size source, xmax_media_size maximum);

    /**
     * Validates camera targets against the selected model without substituting dimensions.
     *
     * @param[in] format The requested camera output dimensions and frame rate.
     * @param[in] model The selected model capability snapshot.
     */
    static xmax_video_format camera_input(
        xmax_video_format format,
        const xmax_model_info& model = RealtimeModelDefinition::default_capabilities
    ) {
        const auto result = input(format, model);
        require(result.fps <= uint32_t(std::numeric_limits<int32_t>::max()), XMAX_INVALID_ARGUMENT,
                "Camera frame rate exceeds the supported integer range");
        media_size({double(result.width), double(result.height)}, true, model);
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
        const xmax_model_info& model = RealtimeModelDefinition::default_capabilities
    ) {
        validate_format(format);
        const auto size = generation_size({double(format.width), double(format.height)}, model);
        return {uint32_t(size.width), uint32_t(size.height), format.fps};
    }
};
} // namespace xmax::detail
