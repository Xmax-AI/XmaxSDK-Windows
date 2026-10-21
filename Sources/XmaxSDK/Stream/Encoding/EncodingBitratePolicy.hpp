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
 * 根据已验证的视频规格计算策略最小码率与估计最大码率。
 *
 * @param[in] f 视频尺寸和帧率，宽高须为偶数、总像素不超过 2100000、帧率范围为 [1, 60]。
 * @return 以 kbps 为单位的策略最小码率和估计最大码率。
 */
std::pair<int, int> bitrate(
    xmax_video_format f
);
} // namespace xmax::detail
