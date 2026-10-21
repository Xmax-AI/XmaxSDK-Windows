//
//  VideoFrame.hpp
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
 * 拥有视频像素及其布局信息；对外视图借用本对象的存储，不转移像素所有权。
 */
struct Frame {

    /**
     * 当前帧的像素格式，决定平面数量及各平面的布局。
     */
    xmax_pixel_format format = XMAX_BGRA;

    /**
     * 视频宽度，单位为像素。
     */
    uint32_t width = 0;

    /**
     * 视频高度，单位为像素。
     */
    uint32_t height = 0;

    /**
     * 严格递增的单调微秒时间戳，用于同一输入绑定内的帧排序。
     */
    int64_t timestamp_us = 0;

    /**
     * 视频平面存储，平面数量和布局由像素格式决定。
     */
    std::vector<uint8_t> planes[3];

    /**
     * 各平面的行步幅，单位为字节，不是像素数。
     */
    size_t strides[3]{};

    /**
     * 借用本帧的只读像素布局，视图不延长像素存储的生命周期。
     *
     * @return 借用本帧像素的只读视图，在本帧被修改或销毁前有效。
     */
    xmax_video_frame view() const;

    /**
     * 校验外部视频视图，并将有效像素行复制到 SDK 拥有的存储。
     *
     * @param[in] source 需校验并复制的只读像素视图；各平面的可访问长度必须完整声明。
     * @return 拥有像素副本的视频帧，不再依赖原始平面存储。
     */
    static Frame copy(
        const xmax_video_frame& source
    );
};

/**
 * 校验非零偶数尺寸、像素预算和帧率范围。
 *
 * @param[in] f 宽、高均为 [2, 8192] 内的偶数，总像素不超过 2100000，帧率为 [1, 60]。
 */
void validate_format(
    xmax_video_format f
);

/**
 * 校验像素格式、平面指针、字节步幅、可访问长度和视频尺寸。
 *
 * @param[in] s 待校验的只读视频视图，像素存储在本次校验期间有效。
 */
void validate_frame(
    const xmax_video_frame& s
);
} // namespace xmax::detail
