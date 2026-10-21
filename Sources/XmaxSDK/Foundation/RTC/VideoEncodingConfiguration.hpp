//
//  VideoEncodingConfiguration.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * 保存已解析的编码尺寸、帧率、码率限制与降级偏好。
 */
struct VideoEncodingConfiguration {

    /**
     * 原生编码器当前使用的像素宽高和每秒帧数。
     */
    xmax_video_format format;

    /**
     * 生效的最小编码码率，单位为 kbps。
     */
    int minimum_bitrate = 100;

    /**
     * 生效的最大编码码率，单位为 kbps。
     */
    int maximum_bitrate = 100;

    /**
     * 编码降级偏好，使用 XMAX_ENCODER_* 常量。
     */
    int preference = XMAX_ENCODER_MAINTAIN_FRAMERATE;
};
} // namespace xmax::detail
