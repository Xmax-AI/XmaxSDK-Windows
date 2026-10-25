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
     * The X2.1 realtime generation model.
     */
    x2_1
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
    if (model != RealtimeModel::x2_1)
        return info;
    info.display_name = "X2.1";
    info.supported_formats[0] = {1024, 1920, 30};
    info.supported_formats[1] = {1920, 1024, 30};
    info.format_count = 2;
    info.minimum_input_pixels = 200000;
    info.maximum_input_pixels = 2100000;
    info.input_size_alignment = 32;
    info.default_frame_rate = 30;
    info.default_camera_format = info.supported_formats[0];
    return info;
}
} // namespace xmax
