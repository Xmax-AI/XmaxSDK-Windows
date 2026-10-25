//
//  RealtimeConfiguration.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "RealtimeModel.hpp"

namespace xmax {

/**
 * The model and initial quality-adaptation policy for an individual realtime controller.
 */
struct RealtimeConfiguration {

    /**
     * The model used by this realtime controller.
     */
    RealtimeModel model = RealtimeModel::x2_1;

    /**
     * Whether connections adapt uplink and downlink quality automatically. Disabled connections start at
     * L1 without reading or writing quality history; manual level changes remain available.
     */
    bool is_adaptive_quality_enabled = true;
};
} // namespace xmax
