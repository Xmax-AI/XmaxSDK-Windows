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
 * Owns video pixels and their layout; external views borrow storage without transferring ownership.
 */
struct Frame {

    /**
     * The pixel format determining plane count and layout.
     */
    xmax_pixel_format format = XMAX_BGRA;

    /**
     * The video width, in pixels.
     */
    uint32_t width = 0;

    /**
     * The video height, in pixels.
     */
    uint32_t height = 0;

    /**
     * The strictly increasing monotonic timestamp, in microseconds, within one input binding.
     */
    int64_t timestamp_us = 0;

    /**
     * The horizontal presentation-mirror flag; pixels and publication direction are unchanged.
     */
    bool presentation_mirrored = false;

    /**
     * The video plane storage, with plane count and layout determined by pixel format.
     */
    std::vector<uint8_t> planes[3];

    /**
     * The row stride of each plane, in bytes rather than pixels.
     */
    size_t strides[3]{};

    /**
     * Returns a borrowed pixel-layout view without extending storage lifetime.
     *
     * @return A read-only pixel view valid until this frame is modified or destroyed.
     */
    xmax_video_frame view() const;

    /**
     * Validates an external frame view and copies its active rows into owned storage.
     *
     * @param[in] source The read-only view to validate and copy; each plane must declare its complete
     * accessible size.
     *
     * @return A frame owning its pixels independently of the source planes.
     */
    static Frame copy(
        const xmax_video_frame& source
    );
};

/**
 * Validates video layout dimensions and frame rate independently of generation model limits.
 *
 * @param[in] f Even dimensions in [2, 8192] and a frame rate in [1, 60].
 */
void validate_format(
    xmax_video_format f
);

/**
 * Validates pixel format, plane pointers, byte strides, accessible sizes, and dimensions.
 *
 * @param[in] s The read-only frame view; pixel storage remains valid during validation.
 */
void validate_frame(
    const xmax_video_frame& s
);
} // namespace xmax::detail
