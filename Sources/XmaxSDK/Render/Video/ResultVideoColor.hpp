//
//  ResultVideoColor.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

// True when no pixel conversion is needed for the supported BT.709 SDR interpretation.
bool result_video_color_is_native(const Frame& frame);

// Normalizes supported SDR BGRA to full-range BT.709. Unsupported input is left intact.
bool prepare_result_video_color(Frame& frame);

} // namespace xmax::detail
