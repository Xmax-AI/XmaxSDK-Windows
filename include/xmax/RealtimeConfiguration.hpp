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
 * 实时控制器的独立业务配置。
 */
struct RealtimeConfiguration {

    /**
     * 本次实时业务使用的模型。
     */
    RealtimeModel model = RealtimeModel::x2_1;
};
} // namespace xmax
