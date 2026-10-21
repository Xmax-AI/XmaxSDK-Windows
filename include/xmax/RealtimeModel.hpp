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
 * SDK 当前支持的实时生成模型。
 */
enum class RealtimeModel {

    /**
     * X2.1 实时生成模型。
     */
    x2_1
};

/**
 * 返回服务端接受的模型标识；返回文本具有静态生命周期。
 *
 * @param[in] model SDK 支持的模型。
 * @return 未识别的枚举值返回空文本，应由创建接口拒绝。
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
 * 读取模型支持的视频规格与默认值，不创建客户端或网络会话。
 *
 * @param[in] model SDK 支持的实时模型。
 * @return 模型能力快照；未知枚举值返回空标识和零规格。
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
    info.supported_formats[0] = {1920, 1080, 30};
    info.supported_formats[1] = {1080, 1920, 30};
    info.supported_formats[2] = {1408, 1408, 30};
    info.supported_formats[3] = {1152, 1536, 30};
    info.supported_formats[4] = {1536, 1152, 30};
    info.format_count = 5;
    info.minimum_input_pixels = 600000;
    info.maximum_input_pixels = 2100000;
    info.input_size_alignment = 32;
    info.default_frame_rate = 30;
    info.default_camera_format = info.supported_formats[1];
    return info;
}
} // namespace xmax
