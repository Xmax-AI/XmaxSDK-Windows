//
//  ApiLogger.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Network/HttpTransport.hpp"

namespace xmax::detail {

/**
 * 记录 HTTP 状态、耗时、大小和经过筛选脱敏的错误信息。
 */
struct ApiLogger {

    /**
     * 记录响应状态、耗时及大小，仅为失败响应保留筛选后的错误字段。
     *
     * @param[in] method HTTP 请求方法。
     * @param[in] path 本次请求的路径；输出前移除查询参数和片段并脱敏。
     * @param[in] response 已完成的 HTTP 响应，诊断输出仅保留筛选后的错误内容。
     * @param[in] duration_ms 本次请求耗时，单位为毫秒。
     * @param[in] successful 是否已通过 HTTP 及服务响应校验。
     */
    static void response(
        const std::string& method,
        const std::string& path,
        const HttpResponse& response,
        int64_t duration_ms,
        bool successful
    ) noexcept {
        XmaxLogger::write(LogCategory::API, successful ? XMAX_LOG_DEBUG : XMAX_LOG_ERROR, [&](auto language) {
            auto message = method + " " + XmaxLogger::safe_path(path) + "\n├─ " +
                           XmaxLogger::localized(language, "状态：", "Status: ") +
                           std::to_string(response.status) + "\n├─ " +
                           XmaxLogger::localized(language, "耗时：", "Duration: ") +
                           std::to_string(duration_ms) + " ms\n" + (successful ? "└─ " : "├─ ") +
                           XmaxLogger::localized(language, "响应：", "Response Size: ") +
                           std::to_string(response.body.size()) + " bytes";
            if (!successful)
                message += "\n└─ " + XmaxLogger::localized(language, "正文：", "Response Body: ") + "\n" +
                           XmaxLogger::failure_body(response.body);
            return message;
        });
    }

    /**
     * 记录传输失败的路径、原因和耗时，不改变原请求结果。
     *
     * @param[in] method HTTP 请求方法。
     * @param[in] path 本次请求的路径；输出前移除查询参数和片段并脱敏。
     * @param[in] reason 本次 HTTP 传输失败的原因，输出日志前执行凭证脱敏。
     * @param[in] duration_ms 本次请求耗时，单位为毫秒。
     */
    static void failure(
        const std::string& method,
        const std::string& path,
        const std::string& reason,
        int64_t duration_ms
    ) noexcept {
        XmaxLogger::write(LogCategory::API, XMAX_LOG_ERROR, [&](auto language) {
            return method + " " + XmaxLogger::safe_path(path) + " " +
                   XmaxLogger::localized(language, "失败 (Request Failed)", "Request Failed") + "\n├─ " +
                   XmaxLogger::localized(language, "耗时：", "Duration: ") + std::to_string(duration_ms) +
                   " ms\n└─ " + XmaxLogger::localized(language, "原因：", "Reason: ") + reason;
        });
    }
};
} // namespace xmax::detail
