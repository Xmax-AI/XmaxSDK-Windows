//
//  Config.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Network/Config.hpp"

namespace xmax::detail {

/**
 * Resolves the API root for the environment.
 *
 * @param[in] realtime True selects the realtime-session endpoint; false selects the general API.
 */
std::string Config::base_url(
    bool realtime
) const {
    RealtimeModelDefinition::parse(model);
    const auto region = environment;
    require(
        region == XMAX_ENVIRONMENT_CHINA || region == XMAX_ENVIRONMENT_GLOBAL,
        XMAX_INVALID_ARGUMENT,
        "Invalid service environment"
    );
    if (region == XMAX_ENVIRONMENT_CHINA)
        return "https://cloud.xmax.22duck.cn/open/api/v1";
    return realtime ? "https://api.xmax.ai/open/api/v1" : "https://api.xmax.cloud/open/api/v1";
}
} // namespace xmax::detail
