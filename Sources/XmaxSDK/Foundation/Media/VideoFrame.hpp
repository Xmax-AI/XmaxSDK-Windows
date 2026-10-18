#pragma once
#include "Foundation/Common.hpp"
namespace xmax::detail {
struct Frame {
    xmax_pixel_format format = XMAX_BGRA;
    uint32_t width = 0, height = 0;
    int64_t timestamp_us = 0;
    std::vector<uint8_t> planes[3];
    size_t strides[3]{};
    xmax_video_frame view() const;
    static Frame copy(const xmax_video_frame& source);
};
void validate_format(xmax_video_format);
void validate_frame(const xmax_video_frame&);
} // namespace xmax::detail
