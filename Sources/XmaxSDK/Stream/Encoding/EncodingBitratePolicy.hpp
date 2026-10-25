//
//  EncodingBitratePolicy.hpp
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
 * Calculates the policy minimum bitrate and estimated maximum from a validated format.
 *
 * @param[in] f Even video dimensions totaling at most 2100000 pixels, with a frame rate in [1, 60].
 *
 * @return The policy minimum and estimated maximum bitrates, in kbps.
 */
std::pair<int, int> bitrate(
    xmax_video_format f
);
} // namespace xmax::detail
