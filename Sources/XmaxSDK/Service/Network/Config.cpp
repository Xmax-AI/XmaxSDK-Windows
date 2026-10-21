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
 * 根据服务环境解析通用 API 或实时会话地址。
 *
 * @param[in] realtime 是否使用实时会话地址。
 */
std::string Config::base_url(
    bool realtime
) const {
    RealtimeModelDefinition::parse(model);
    const auto region = environment;
    require(
        region == XMAX_ENVIRONMENT_CHINA || region == XMAX_ENVIRONMENT_GLOBAL, XMAX_INVALID_ARGUMENT,
        "Invalid service environment"
    );
    if (region == XMAX_ENVIRONMENT_CHINA)
        return "https://cloud.xmax.22duck.cn/open/api/v1";
    return realtime ? "https://api.xmax.ai/open/api/v1" : "https://api.xmax.cloud/open/api/v1";
}
} // namespace xmax::detail
