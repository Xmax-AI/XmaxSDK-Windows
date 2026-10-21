//
//  XmaxEnvironment.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "xmax.h"

namespace xmax {

/**
 * 客户端的服务环境，独立于实时模型。
 */
enum class XmaxEnvironment {

    /**
     * 国内环境。
     */
    china = XMAX_ENVIRONMENT_CHINA,

    /**
     * 海外环境。
     */
    global = XMAX_ENVIRONMENT_GLOBAL
};
} // namespace xmax
