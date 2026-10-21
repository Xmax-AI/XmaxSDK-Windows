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
 * 将支持的视频帧转换或复制为 SDK 拥有的 BGRA 帧。
 *
 * @param[in] source 待转换的有效帧，源像素在本次同步调用返回前保持有效。
 * @return 拥有独立像素存储的 BGRA 帧，保留输入尺寸和时间戳。
 */
Frame bgra_frame(
    const Frame& source
);

} // namespace xmax::detail
