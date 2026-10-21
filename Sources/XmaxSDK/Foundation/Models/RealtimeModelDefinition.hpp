//
//  RealtimeModelDefinition.hpp
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
 * 统一解析实时模型标识和媒体能力，供各层读取只读规则。
 */
class RealtimeModelDefinition {

  public:

    /**
     * 模型支持的编码目标和生成画布，顺序与摄像头预设一致。
     */
    inline static constexpr auto capabilities = model_capabilities(RealtimeModel::x2_1);

    /**
     * 模型支持的编码目标，由公开模型描述读取。
     */
    inline static constexpr std::array<xmax_video_format, 5> formats{
        {capabilities.supported_formats[0], capabilities.supported_formats[1],
         capabilities.supported_formats[2], capabilities.supported_formats[3],
         capabilities.supported_formats[4]}
    };

    /**
     * 本地媒体帧允许的最大总像素数。
     */
    inline static constexpr uint32_t maximum_input_pixels = capabilities.maximum_input_pixels;

    /**
     * 解析服务端模型标识，拒绝未知值。
     *
     * @param[in] identifier 模型标识，不允许空文本。
     * @return 对应的强类型模型。
     */
    static RealtimeModel parse(
        std::string_view identifier
    ) {
        if (identifier == model_identifier(RealtimeModel::x2_1))
            return RealtimeModel::x2_1;
        throw Error(XMAX_INVALID_ARGUMENT, "Unsupported realtime model");
    }

    /**
     * 读取模型能力快照，返回的文本为静态数据。
     *
     * @param[in] model 已解析的模型。
     * @return 包含支持尺寸、像素限制和默认值的模型能力快照。
     */
    static xmax_model_info info(
        RealtimeModel model
    ) {
        require(model == RealtimeModel::x2_1, XMAX_INVALID_ARGUMENT, "Unsupported realtime model");
        return model_capabilities(model);
    }

    /**
     * 按预设读取视频规格。
     *
     * @param[in] preset 非空摄像头预设。
     * @param[in] fps 沿用的每秒帧数。
     * @return 预设尺寸与指定帧率组成的视频规格。
     */
    static xmax_video_format preset_format(
        xmax_camera_preset preset,
        uint32_t fps
    ) {
        require(
            preset >= XMAX_CAMERA_PRESET_1920_1080 && preset <= XMAX_CAMERA_PRESET_1536_1152,
            XMAX_INVALID_ARGUMENT, "Invalid camera preset"
        );
        auto format = formats[size_t(preset) - 1];
        format.fps = fps;
        return format;
    }

    /**
     * 查找视频尺寸对应的摄像头预设。
     *
     * @param[in] format 当前摄像头编码规格。
     * @return 没有匹配尺寸时返回 NONE。
     */
    static xmax_camera_preset preset(
        xmax_video_format format
    ) {
        for (size_t i = 0; i < formats.size(); ++i)
            if (formats[i].width == format.width && formats[i].height == format.height)
                return xmax_camera_preset(i + 1);
        return XMAX_CAMERA_PRESET_NONE;
    }
};
} // namespace xmax::detail
