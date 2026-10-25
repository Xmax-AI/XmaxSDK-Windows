//
//  VideoFrame.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/VideoFrame.hpp"
#include "Foundation/Models/RealtimeModelDefinition.hpp"
#include <cstring>

namespace xmax::detail {

/**
 * Validates nonzero even dimensions, pixel budget, and frame-rate limits.
 *
 * @param[in] f Even dimensions in [2, 8192], at most 2100000 pixels, and a frame rate in [1, 60].
 */
void validate_format(
    xmax_video_format f
) {
    require(
        f.width >= 2 && f.height >= 2 && f.width <= 8192 && f.height <= 8192 && f.width % 2 == 0 &&
            f.height % 2 == 0 && f.fps >= 1 && f.fps <= 60 &&
            uint64_t(f.width) * f.height <= RealtimeModelDefinition::maximum_input_pixels,
        XMAX_INVALID_ARGUMENT,
        "Video format requires even dimensions, at most 2100000 pixels, and 1-60 fps"
    );
}

/**
 * Returns a borrowed pixel-layout view without extending storage lifetime.
 *
 * @return A read-only pixel view valid until this frame is modified or destroyed.
 */
xmax_video_frame Frame::view() const {
    xmax_video_frame v{};
    v.struct_size = sizeof(v);
    v.pixel_format = format;
    v.width = width;
    v.height = height;
    v.timestamp_us = timestamp_us;

    for (int p = 0; p < 3; ++p) {
        v.planes[p] = planes[p].data();
        v.strides[p] = strides[p];
        v.plane_sizes[p] = planes[p].size();
    }

    return v;
}

/**
 * Validates pixel format, plane pointers, byte strides, accessible sizes, and dimensions.
 *
 * @param[in] s The read-only frame view; pixel storage remains valid during validation.
 */
void validate_frame(
    const xmax_video_frame& s
) {
    require(
        s.struct_size >= sizeof(s) && s.width >= 2 && s.height >= 2 && s.width <= 8192 && s.height <= 8192 &&
            s.width % 2 == 0 && s.height % 2 == 0 &&
            uint64_t(s.width) * s.height <= RealtimeModelDefinition::maximum_input_pixels &&
            s.timestamp_us >= 0,
        XMAX_INVALID_ARGUMENT,
        "Invalid video frame dimensions or timestamp"
    );
    require(
        s.pixel_format == XMAX_BGRA || s.pixel_format == XMAX_NV12 || s.pixel_format == XMAX_I420,
        XMAX_INVALID_ARGUMENT,
        "Unsupported pixel format"
    );

    const int count = s.pixel_format == XMAX_BGRA ? 1 : (s.pixel_format == XMAX_NV12 ? 2 : 3);

    for (int p = 0; p < count; ++p) {
        size_t rows = p == 0 ? s.height : s.height / 2;
        size_t row_bytes = s.pixel_format == XMAX_BGRA
                               ? size_t(s.width) * 4
                               : (p == 0 || s.pixel_format == XMAX_NV12 ? s.width : s.width / 2);
        require(
            s.planes[p] && s.strides[p] >= row_bytes && s.strides[p] <= 64 * 1024 &&
                s.plane_sizes[p] >= (rows - 1) * s.strides[p] + row_bytes,
            XMAX_INVALID_ARGUMENT,
            "Video plane pointer, stride or accessible size is invalid"
        );
    }
}

/**
 * Validates an external frame view and copies its active rows into owned storage.
 *
 * @param[in] s The read-only pixel view with valid pointers, byte strides, and complete accessible sizes.
 *
 * @return A frame owning its pixels independently of the source planes.
 */
Frame Frame::copy(
    const xmax_video_frame& s
) {
    validate_frame(s);

    Frame f;
    f.format = s.pixel_format;
    f.width = s.width;
    f.height = s.height;
    f.timestamp_us = s.timestamp_us;

    const int count = s.pixel_format == XMAX_BGRA ? 1 : (s.pixel_format == XMAX_NV12 ? 2 : 3);

    for (int p = 0; p < count; ++p) {
        const size_t rows = p == 0 ? s.height : s.height / 2;
        const size_t row_bytes = s.pixel_format == XMAX_BGRA
                                     ? size_t(s.width) * 4
                                     : (p == 0 || s.pixel_format == XMAX_NV12 ? s.width : s.width / 2);
        f.strides[p] = row_bytes;
        f.planes[p].resize(rows * row_bytes);

        for (size_t y = 0; y < rows; ++y)
            std::memcpy(
                f.planes[p].data() + y * row_bytes,
                s.planes[p] + y * s.strides[p],
                row_bytes
            );
    }

    return f;
}
} // namespace xmax::detail
