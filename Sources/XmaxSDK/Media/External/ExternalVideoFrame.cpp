//
//  ExternalVideoFrame.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/External/ExternalVideoFrame.hpp"
#include "Media/External/ExternalVideoResampler.hpp"
#include <algorithm>

namespace xmax::detail {
namespace {

/** A centered source rectangle aligned to 4:2:0 chroma samples. */
struct Crop {
    /** The horizontal and vertical origins in source pixels. */
    uint32_t x, y;
    /** The active width and height in source pixels. */
    uint32_t width, height;
};

/** Resolves the largest chroma-aligned center crop matching the target aspect ratio. */
Crop center_crop(const xmax_video_frame& source, xmax_video_format target) {
    auto width = source.width;
    auto height = source.height;
    if (width * target.height > height * target.width)
        width = std::max(2u, (height * target.width / target.height) & ~1u);
    else if (width * target.height < height * target.width)
        height = std::max(2u, (width * target.height / target.width) & ~1u);
    return {(source.width - width) / 2 & ~1u, (source.height - height) / 2 & ~1u, width, height};
}

/** Resamples one packed or planar source plane into tightly packed owned pixels. */
void resize_plane(
    const xmax_video_frame& source,
    Frame& destination,
    Crop crop,
    unsigned plane,
    uint32_t subsampling,
    size_t channels
) {
    const auto width = destination.width / subsampling;
    const auto height = destination.height / subsampling;
    const auto stride = width * channels;
    destination.strides[plane] = stride;
    auto& output = destination.planes[plane];
    output.resize(stride * height);
    const auto* input = source.planes[plane] + crop.y / subsampling * source.strides[plane] +
                        crop.x / subsampling * channels;
    resample_external_plane(input, source.strides[plane], crop.width / subsampling, crop.height / subsampling,
                            output.data(), width, height, channels);
}

/** Pairs resampled I420 chroma planes to produce NV12. */
void interleave_chroma(Frame& destination) {
    auto u = std::move(destination.planes[1]);
    auto v = std::move(destination.planes[2]);
    destination.strides[1] = destination.width;
    destination.strides[2] = 0;
    auto& uv = destination.planes[1];
    uv.resize(u.size() * 2);
    for (size_t i = 0; i < u.size(); ++i) {
        uv[2 * i] = u[i];
        uv[2 * i + 1] = v[i];
    }
}

} // namespace

Frame copy_external_video_frame(
    const xmax_video_frame& source,
    xmax_video_format format,
    bool normalize_chroma
) {
    const bool convert = normalize_chroma && source.pixel_format == XMAX_I420;
    if (!convert && source.width == format.width && source.height == format.height)
        return Frame::copy(source);

    validate_frame(source);
    Frame destination;
    destination.format = convert ? XMAX_NV12 : source.pixel_format;
    destination.width = format.width;
    destination.height = format.height;
    destination.timestamp_us = source.timestamp_us;

    const auto crop = center_crop(source, format);
    resize_plane(source, destination, crop, 0, 1, source.pixel_format == XMAX_BGRA ? 4 : 1);
    if (source.pixel_format == XMAX_NV12)
        resize_plane(source, destination, crop, 1, 2, 2);
    if (source.pixel_format == XMAX_I420) {
        resize_plane(source, destination, crop, 1, 2, 1);
        resize_plane(source, destination, crop, 2, 2, 1);
        if (convert)
            interleave_chroma(destination);
    }
    return destination;
}

} // namespace xmax::detail
