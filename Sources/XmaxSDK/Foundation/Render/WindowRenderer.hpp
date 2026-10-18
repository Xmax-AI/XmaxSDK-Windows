#pragma once
#include "Foundation/Media/VideoFrame.hpp"
namespace xmax::detail {
bool native_window_available() noexcept;
void render_window(const Frame&, void* window, int32_t content_mode);
} // namespace xmax::detail
