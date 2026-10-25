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
 * Transport quality levels, distinct from network-quality ratings.
 */
enum class RealtimeQualityLevel {

    /**
     * Retains baseline media dimensions.
     */
    l1 = 1,

    /**
     * Targets approximately 90% of baseline width and height.
     */
    l2 = 2,

    /**
     * Targets approximately 75% of baseline width and height.
     */
    l3 = 3,

    /**
     * Targets approximately 60% of baseline width and height.
     */
    l4 = 4,

    /**
     * Targets approximately 50% of baseline width and height and reduces the uplink frame rate.
     */
    l5 = 5
};
} // namespace xmax
