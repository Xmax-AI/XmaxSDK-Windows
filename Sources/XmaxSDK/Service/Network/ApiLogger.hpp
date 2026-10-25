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
 * Logs HTTP status, duration, size, and filtered, redacted error information.
 */
struct ApiLogger {

    /**
     * Logs status, duration, and size, retaining filtered error fields only for failed responses.
     *
     * @param[in] method The HTTP request method.
     * @param[in] path The request path; query parameters and fragments are removed and credentials redacted
     * before logging.
     * @param[in] response The completed HTTP response; diagnostics retain only filtered failure content.
     * @param[in] duration_ms The request duration, in milliseconds.
     * @param[in] successful Whether HTTP and service-response validation succeeded.
     */
    static void response(
        const std::string& method,
        const std::string& path,
        const HttpResponse& response,
        int64_t duration_ms,
        bool successful
    ) noexcept {
        XmaxLogger::write(
            LogCategory::API,
            successful ? XMAX_LOG_DEBUG : XMAX_LOG_ERROR,
            [&](auto language) {
                auto message = method + " " + XmaxLogger::safe_path(path) + "\n├─ " +
                               XmaxLogger::localized(
                                   language,
                                   "状态：",
                                   "Status: "
                               ) +
                               std::to_string(response.status) + "\n├─ " +
                               XmaxLogger::localized(
                                   language,
                                   "耗时：",
                                   "Duration: "
                               ) +
                               std::to_string(duration_ms) + " ms\n" + (successful ? "└─ " : "├─ ") +
                               XmaxLogger::localized(
                                   language,
                                   "响应：",
                                   "Response Size: "
                               ) +
                               std::to_string(response.body.size()) + " bytes";
                if (!successful)
                    message += "\n└─ " + XmaxLogger::localized(
                        language,
                        "正文：",
                        "Response Body: "
                    ) + "\n" +
                               XmaxLogger::failure_body(response.body);
                return message;
            }
        );
    }

    /**
     * Logs transport failure path, cause, and duration without changing the request result.
     *
     * @param[in] method The HTTP request method.
     * @param[in] path The request path; query parameters and fragments are removed and credentials redacted
     * before logging.
     * @param[in] reason The HTTP transport failure reason, redacted before logging.
     * @param[in] duration_ms The request duration, in milliseconds.
     */
    static void failure(
        const std::string& method,
        const std::string& path,
        const std::string& reason,
        int64_t duration_ms
    ) noexcept {
        XmaxLogger::write(
            LogCategory::API,
            XMAX_LOG_ERROR,
            [&](auto language) {
                return method + " " + XmaxLogger::safe_path(path) + " " +
                       XmaxLogger::localized(
                           language,
                           "失败 (Request Failed)",
                           "Request Failed"
                       ) +
                       "\n├─ " +
                       XmaxLogger::localized(
                           language,
                           "耗时：",
                           "Duration: "
                       ) +
                       std::to_string(duration_ms) +
                       " ms\n└─ " + XmaxLogger::localized(
                           language,
                           "原因：",
                           "Reason: "
                       ) + reason;
            }
        );
    }
};
} // namespace xmax::detail
