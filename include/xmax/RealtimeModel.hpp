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
#include "xmax.h"

namespace xmax {

/**
 * The realtime generation models supported by XmaxSDK.
 */
enum class RealtimeModel {

    /**
     * The X2.1 Agora model with five standard aspect ratios.
     */
    x2_1,

    /**
     * The X2.1 Flash model using Agora.
     */
    x2_1_flash
};

/**
 * Returns the server model identifier as a string with static lifetime.
 *
 * @param[in] model The supported SDK model.
 *
 * @return Unknown enum values return an empty string and must be rejected by creation APIs.
 */
inline constexpr const char* model_identifier(
    RealtimeModel model
) noexcept {
    switch (model) {
    case RealtimeModel::x2_1:
        return "x2.1";
    case RealtimeModel::x2_1_flash:
        return "x2.1-flash";
    }
    return "";
}

/**
 * Returns supported video formats and defaults without creating a client or network session.
 *
 * @param[in] model The supported realtime model.
 *
 * @return The capability snapshot; unknown values return an empty identifier and zero formats.
 */
inline constexpr xmax_model_info model_capabilities(
    RealtimeModel model
) noexcept {
    xmax_model_info info{};
    info.struct_size = sizeof(info);
    info.identifier = model_identifier(model);
    info.display_name = "";
    switch (model) {
    case RealtimeModel::x2_1:
        info.display_name = "X2.1";
        info.supported_formats[0] = {1920, 1080, 30};
        info.supported_formats[1] = {1080, 1920, 30};
        info.supported_formats[2] = {1408, 1408, 30};
        info.supported_formats[3] = {1152, 1536, 30};
        info.supported_formats[4] = {1536, 1152, 30};
        info.minimum_input_pixels = 200000;
        info.maximum_input_pixels = 2100000;
        break;
    case RealtimeModel::x2_1_flash:
        info.display_name = "X2.1 Flash";
        info.supported_formats[0] = {704, 1280, 30};
        info.supported_formats[1] = {1280, 704, 30};
        info.supported_formats[2] = {960, 960, 30};
        info.supported_formats[3] = {1024, 768, 30};
        info.supported_formats[4] = {768, 1024, 30};
        info.minimum_input_pixels = 600000;
        info.maximum_input_pixels = 960 * 960;
        break;
    default:
        return info;
    }
    info.format_count = 5;
    info.input_size_alignment = 32;
    info.default_frame_rate = 30;
    info.default_camera_format = info.supported_formats[0];
    return info;
}
} // namespace xmax
