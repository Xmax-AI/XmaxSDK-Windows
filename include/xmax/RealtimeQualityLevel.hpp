//
//  RealtimeQualityLevel.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once

namespace xmax {

/**
 * 传输画质档位，与网络质量评级分别定义。
 */
enum class RealtimeQualityLevel {

    /**
     * 保留媒体基准尺寸。
     */
    l1 = 1,

    /**
     * 目标宽高约为媒体基准的 80%。
     */
    l2 = 2,

    /**
     * 目标宽高约为媒体基准的 65%。
     */
    l3 = 3,

    /**
     * 目标宽高约为媒体基准的 50%。
     */
    l4 = 4,

    /**
     * 使用四级尺寸，并进一步降低上行帧率。
     */
    l5 = 5
};
} // namespace xmax
