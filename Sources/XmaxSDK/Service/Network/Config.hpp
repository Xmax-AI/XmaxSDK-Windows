//
//  Config.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Realtime/RealtimeModel.hpp"

namespace xmax::detail {

/**
 * The copied API credentials, model identifier, and HTTP timeout.
 */
struct Config {

    /**
     * The copied API credentials; never write them to unredacted logs.
     */
    std::string key;

    /**
     * The realtime model identifier; x2.1.1-preview-cn and x2.1.1-preview-us are supported.
     */
    std::string model = "x2.1.1-preview-cn";

    /**
     * The HTTP request timeout, in milliseconds.
     */
    uint32_t timeout_ms = 15000;

    /**
     * The general and realtime API environment, defaulting to China.
     */
    xmax_environment environment = XMAX_ENVIRONMENT_CHINA;

    /**
     * Resolves the API root for the environment.
     *
     * @param[in] realtime True selects the realtime-session endpoint; false selects the general API.
     */
    std::string base_url(
        bool realtime = false
    ) const;
};
} // namespace xmax::detail
