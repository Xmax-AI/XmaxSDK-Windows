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
#include "Foundation/Models/RealtimeModelDefinition.hpp"

namespace xmax::detail {

/**
 * 保存复制后的 API 凭证、模型标识和 HTTP 超时配置。
 */
struct Config {

    /**
     * 已复制的 API 凭证，不得输出到未脱敏日志。
     */
    std::string key;

    /**
     * 实时生成模型标识，当前仅支持 x2.1。
     */
    std::string model = "x2.1";

    /**
     * HTTP 请求超时，单位为毫秒。
     */
    uint32_t timeout_ms = 15000;

    /**
     * 通用 API 和实时会话使用的服务环境，默认国内。
     */
    xmax_environment environment = XMAX_ENVIRONMENT_CHINA;

    /**
     * 根据环境解析服务 API 根地址。
     *
     * @param[in] realtime true 使用实时会话地址，false 使用通用 API 地址。
     */
    std::string base_url(
        bool realtime = false
    ) const;
};
} // namespace xmax::detail
