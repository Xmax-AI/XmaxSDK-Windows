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
 * The client's service environment, independent of its realtime model.
 */
enum class XmaxEnvironment {

    /**
     * The China service environment.
     */
    china = XMAX_ENVIRONMENT_CHINA,

    /**
     * The global service environment.
     */
    global = XMAX_ENVIRONMENT_GLOBAL
};
} // namespace xmax
