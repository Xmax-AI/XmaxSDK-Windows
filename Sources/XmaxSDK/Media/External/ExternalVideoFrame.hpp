//
//  ExternalVideoFrame.hpp
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

/**
 * Copies and resizes source pixels, interleaving legacy I420 input for typed sources.
 */
Frame copy_external_video_frame(
    const xmax_video_frame& source,
    xmax_video_format format,
    bool normalize_chroma
);

} // namespace xmax::detail
