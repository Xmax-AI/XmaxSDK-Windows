//
//  RealtimeModel.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <array>
#include <algorithm>
#include <string_view>
#include <xmax/RealtimeModel.hpp>

namespace xmax::detail {

/**
 * Resolves realtime model identifiers and exposes read-only media capabilities across layers.
 */
class RealtimeModelDefinition {

  public:

    /**
     * The model's encoding targets and generation canvases, ordered by camera preset.
     */
    inline static constexpr auto capabilities = model_capabilities(RealtimeModel::x2_1);

    /**
     * The model's encoding targets, read from its public description.
     */
    inline static constexpr std::array<xmax_video_format, 5> formats{
        {capabilities.supported_formats[0], capabilities.supported_formats[1], capabilities.supported_formats[2],
         capabilities.supported_formats[3], capabilities.supported_formats[4]}
    };

    /**
     * The maximum total pixels permitted in a local media frame.
     */
    inline static constexpr uint32_t maximum_input_pixels = capabilities.maximum_input_pixels;

    /**
     * Parses a server model identifier and rejects unknown values.
     *
     * @param[in] identifier The nonempty model identifier.
     *
     * @return The corresponding strongly typed model.
     */
    static RealtimeModel parse(
        std::string_view identifier
    ) {
        if (identifier == model_identifier(RealtimeModel::x2_1))
            return RealtimeModel::x2_1;
        throw Error(
            XMAX_INVALID_ARGUMENT,
            "Unsupported realtime model"
        );
    }

    /**
     * Returns a model capability snapshot whose text references static storage.
     *
     * @param[in] model The parsed model.
     *
     * @return The supported dimensions, pixel limits, and defaults for the model.
     */
    static xmax_model_info info(
        RealtimeModel model
    ) {
        require(
            model == RealtimeModel::x2_1,
            XMAX_INVALID_ARGUMENT,
            "Unsupported realtime model"
        );
        return model_capabilities(model);
    }

    /**
     * Returns a video format for a preset.
     *
     * @param[in] preset A nonempty camera preset.
     * @param[in] fps The frame rate to retain, in frames per second.
     *
     * @return The preset dimensions with the requested frame rate.
     */
    static xmax_video_format preset_format(
        xmax_camera_preset preset,
        uint32_t fps
    ) {
        xmax_video_format format{};
        switch (preset) {
        case XMAX_CAMERA_PRESET_1920_1080:
            format = {1920, 1080, fps};
            break;
        case XMAX_CAMERA_PRESET_1080_1920:
            format = {1080, 1920, fps};
            break;
        case XMAX_CAMERA_PRESET_1408_1408:
            format = {1408, 1408, fps};
            break;
        case XMAX_CAMERA_PRESET_1152_1536:
            format = {1152, 1536, fps};
            break;
        case XMAX_CAMERA_PRESET_1536_1152:
            format = {1536, 1152, fps};
            break;
        default:
            throw Error(
                XMAX_INVALID_ARGUMENT,
                "This camera preset is not supported by the model"
            );
        }
        return format;
    }

    /**
     * Finds the camera preset matching video dimensions.
     *
     * @param[in] format The current camera encoding format.
     *
     * @return NONE if no dimensions match.
     */
    static xmax_camera_preset preset(
        xmax_video_format format
    ) {
        if (format.width == 1920 && format.height == 1080)
            return XMAX_CAMERA_PRESET_1920_1080;
        if (format.width == 1080 && format.height == 1920)
            return XMAX_CAMERA_PRESET_1080_1920;
        if (format.width == 1408 && format.height == 1408)
            return XMAX_CAMERA_PRESET_1408_1408;
        if (format.width == 1152 && format.height == 1536)
            return XMAX_CAMERA_PRESET_1152_1536;
        if (format.width == 1536 && format.height == 1152)
            return XMAX_CAMERA_PRESET_1536_1152;
        return XMAX_CAMERA_PRESET_NONE;
    }
};
} // namespace xmax::detail
