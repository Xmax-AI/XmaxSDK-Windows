//
//  XmaxConfiguration.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "XmaxEnvironment.hpp"
#include <string>

namespace xmax {

/**
 * The global configuration used to create a client.
 */
struct XmaxConfiguration {

    /**
     * The API credential copied at creation; empty permits local preparation only.
     */
    std::string api_key;

    /**
     * The service environment for general and realtime APIs.
     */
    XmaxEnvironment environment = XmaxEnvironment::china;

    /**
     * The process-wide logging options, disabled by default.
     */
    uint32_t logger_options = XMAX_LOG_NONE;

    /**
     * The HTTP timeout, in milliseconds.
     */
    uint32_t http_timeout_ms = 15000;
};
} // namespace xmax
