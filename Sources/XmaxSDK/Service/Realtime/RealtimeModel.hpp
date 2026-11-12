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
#include <string_view>
#include <xmax/RealtimeModel.hpp>

namespace xmax::detail {

/**
 * Resolves realtime model identifiers and exposes read-only media capabilities across layers.
 */
class RealtimeModelDefinition {

    struct CameraPreset {
        xmax_camera_preset preset;
        uint32_t width;
        uint32_t height;
    };

    inline static constexpr CameraPreset camera_presets[]{
        {XMAX_CAMERA_PRESET_1920_1080, 1920, 1080},
        {XMAX_CAMERA_PRESET_1080_1920, 1080, 1920},
        {XMAX_CAMERA_PRESET_1408_1408, 1408, 1408},
        {XMAX_CAMERA_PRESET_1152_1536, 1152, 1536},
        {XMAX_CAMERA_PRESET_1536_1152, 1536, 1152},
        {XMAX_CAMERA_PRESET_704_1280, 704, 1280},
        {XMAX_CAMERA_PRESET_1280_704, 1280, 704},
        {XMAX_CAMERA_PRESET_960_960, 960, 960},
        {XMAX_CAMERA_PRESET_1024_768, 1024, 768},
        {XMAX_CAMERA_PRESET_768_1024, 768, 1024}
    };

  public:

    /**
     * The default model's encoding targets and generation canvases.
     */
    inline static constexpr auto default_capabilities = model_capabilities(RealtimeModel::x2_1);

    /**
     * The default model's encoding targets, read from its public description.
     */
    inline static constexpr std::array<xmax_video_format, 5> formats{
        {default_capabilities.supported_formats[0], default_capabilities.supported_formats[1],
         default_capabilities.supported_formats[2], default_capabilities.supported_formats[3],
         default_capabilities.supported_formats[4]}
    };

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
        if (identifier == model_identifier(RealtimeModel::x2_1_flash))
            return RealtimeModel::x2_1_flash;
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
        const auto capabilities = model_capabilities(model);
        require(
            capabilities.format_count != 0,
            XMAX_INVALID_ARGUMENT,
            "Unsupported realtime model"
        );
        return capabilities;
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
        for (const auto& candidate : camera_presets)
            if (candidate.preset == preset)
                return {candidate.width, candidate.height, fps};
        throw Error(XMAX_INVALID_ARGUMENT, "This camera preset is not supported by the model");
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
        for (const auto& candidate : camera_presets)
            if (candidate.width == format.width && candidate.height == format.height)
                return candidate.preset;
        return XMAX_CAMERA_PRESET_NONE;
    }
};
} // namespace xmax::detail
