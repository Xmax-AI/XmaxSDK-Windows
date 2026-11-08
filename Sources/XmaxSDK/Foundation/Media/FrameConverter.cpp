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
#include <cstring>

namespace xmax::detail {

/**
 * Converts or copies a supported frame to owned BGRA pixels.
 *
 * @param[in] source The valid source frame; its pixels must remain valid until this synchronous call
 * returns.
 *
 * @return An independently owned BGRA frame preserving source dimensions and timestamp.
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

Frame rotate_bgra(Frame frame, unsigned degrees) {
    if (degrees != 90 && degrees != 180 && degrees != 270)
        return frame;
    Frame result;
    result.width = degrees == 180 ? frame.width : frame.height;
    result.height = degrees == 180 ? frame.height : frame.width;
    result.timestamp_us = frame.timestamp_us;
    result.presentation_mirrored = frame.presentation_mirrored;
    result.color_primaries = frame.color_primaries;
    result.color_transfer = frame.color_transfer;
    result.strides[0] = size_t(result.width) * 4;
    result.planes[0].resize(result.strides[0] * result.height);
    for (uint32_t y = 0; y < frame.height; ++y) {
        for (uint32_t x = 0; x < frame.width; ++x) {
            const auto column = degrees == 270 ? y : degrees == 180 ? frame.width - 1 - x : frame.height - 1 - y;
            const auto row = degrees == 90 ? x : degrees == 180 ? frame.height - 1 - y : frame.width - 1 - x;
            std::memcpy(result.planes[0].data() + row * result.strides[0] + column * 4,
                        frame.planes[0].data() + y * frame.strides[0] + x * 4, 4);
        }
    }
    return result;
}

} // namespace xmax::detail
