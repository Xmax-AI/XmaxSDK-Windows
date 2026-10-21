//
//  FrameConverter.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/FrameConverter.hpp"
#include <algorithm>

namespace xmax::detail {

/**
 * 将支持的视频帧转换或复制为 SDK 拥有的 BGRA 帧。
 *
 * @param[in] source 待转换的有效帧，源像素在本次同步调用返回前保持有效。
 * @return 拥有独立像素存储的 BGRA 帧，保留输入尺寸和时间戳。
 */
Frame bgra_frame(
    const Frame& source
) {
    if (source.format == XMAX_BGRA)
        return Frame::copy(source.view());

    validate_frame(source.view());

    Frame result;
    result.width = source.width;
    result.height = source.height;
    result.timestamp_us = source.timestamp_us;
    result.strides[0] = size_t(result.width) * 4;
    result.planes[0].resize(result.strides[0] * result.height);

    const auto byte = [](int value) { return uint8_t(std::clamp(value, 0, 255)); };

    for (uint32_t y = 0; y < source.height; ++y)
        for (uint32_t x = 0; x < source.width; ++x) {
            const int luma = int(source.planes[0][y * source.strides[0] + x]) - 16;
            int u, v;

            if (source.format == XMAX_NV12) {
                const auto index = (y / 2) * source.strides[1] + (x / 2) * 2;
                u = int(source.planes[1][index]) - 128;
                v = int(source.planes[1][index + 1]) - 128;
            } else {
                u = int(source.planes[1][(y / 2) * source.strides[1] + x / 2]) - 128;
                v = int(source.planes[2][(y / 2) * source.strides[2] + x / 2]) - 128;
            }

            auto* pixel = result.planes[0].data() + y * result.strides[0] + x * 4;
            pixel[0] = byte((298 * luma + 516 * u + 128) >> 8);
            pixel[1] = byte((298 * luma - 100 * u - 208 * v + 128) >> 8);
            pixel[2] = byte((298 * luma + 409 * v + 128) >> 8);
            pixel[3] = 255;
        }

    return result;
}

} // namespace xmax::detail
