//
//  FrameConverter.hpp
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
 * Converts or copies a supported frame to owned BGRA pixels.
 *
 * @param[in] source The valid source frame; its pixels must remain valid until this synchronous call
 * returns.
 *
 * @return An independently owned BGRA frame preserving source dimensions and timestamp.
 */
Frame bgra_frame(
    const Frame& source
);

} // namespace xmax::detail
