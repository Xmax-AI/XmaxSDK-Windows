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
 * @param[in] f Even video dimensions totaling at most 2100000 pixels, with a positive frame rate.
 *
 * @return The policy minimum and estimated maximum bitrates, in kbps.
 */
std::pair<int, int> bitrate(
    xmax_video_format f
);

/**
 * Resolves the server's downlink bitrate bounds at its 24 fps reference rate.
 *
 * @param[in] format The requested return dimensions. The frame rate is ignored.
 * @return The 500 kbps minimum and the estimated maximum clamped to 500 through 10000 kbps.
 */
std::pair<int, int> downlink_bitrate(
    xmax_video_format format
);

} // namespace xmax::detail
