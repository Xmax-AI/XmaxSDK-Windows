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
 * 创建客户端时使用的全局业务配置。
 */
struct XmaxConfiguration {

    /**
     * 创建时复制的 API 凭证，空文本仅允许本地准备。
     */
    std::string api_key;

    /**
     * 通用 API 与实时会话的服务环境。
     */
    XmaxEnvironment environment = XmaxEnvironment::china;

    /**
     * 进程级日志选项；默认关闭。
     */
    uint32_t logger_options = XMAX_LOG_NONE;

    /**
     * HTTP 超时，单位为毫秒。
     */
    uint32_t http_timeout_ms = 15000;
};
} // namespace xmax
