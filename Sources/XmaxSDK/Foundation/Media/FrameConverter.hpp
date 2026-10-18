#pragma once
#include "Foundation/Media/VideoFrame.hpp"
namespace xmax::detail {
// CPU fallback shared by SDK-owned capture, preview and native window rendering.
Frame bgra_frame(const Frame&);
Frame fit_frame(Frame, xmax_video_format);
} // namespace xmax::detail
