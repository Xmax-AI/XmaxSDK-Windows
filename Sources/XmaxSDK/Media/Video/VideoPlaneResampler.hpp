//
//  VideoPlaneResampler.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <cstddef>
#include <cstdint>

namespace xmax::detail {

/**
 * Resamples a cropped plane with an antialiasing filter, keeping each pixel or UV pair together.
 *
 * @param[in] source The first sample of the source crop.
 * @param[in] stride The byte stride between source rows.
 * @param[in] source_width The crop width in samples.
 * @param[in] source_height The crop height in rows.
 * @param[out] destination The tightly packed output plane.
 * @param[in] width The output width in samples.
 * @param[in] height The output height in rows.
 * @param[in] channels The number of bytes per sample, 1 for planar data, 2 for UV, or 4 for BGRA.
 */
void resample_video_plane(
    const uint8_t* source,
    size_t stride,
    uint32_t source_width,
    uint32_t source_height,
    uint8_t* destination,
    uint32_t width,
    uint32_t height,
    size_t channels
);

} // namespace xmax::detail
