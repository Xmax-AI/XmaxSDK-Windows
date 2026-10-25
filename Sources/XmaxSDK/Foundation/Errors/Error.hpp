//
//  Error.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <xmax/xmax.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace xmax::detail {

/**
 * An internal exception carrying an SDK result and diagnostic, translated by the C interface.
 */
struct Error : std::runtime_error {

    /**
     * The SDK error code carried by this exception.
     */
    xmax_result code;

    /**
     * The service error code, or 0 if absent.
     */
    int32_t api_code = 0;

    /**
     * The HTTP response status, or 0 if no response was received.
     */
    int32_t http_status = 0;

    /**
     * Creates an internal exception with an SDK result and diagnostic text.
     *
     * @param[in] c The SDK error code retained by this exception.
     * @param[in] message The diagnostic message copied into this exception at construction.
     * @param[in] api The service error code, or 0 if absent.
     * @param[in] http The HTTP response status, or 0 if no response was received.
     */
    Error(
        xmax_result c,
        std::string message,
        int32_t api = 0,
        int32_t http = 0
    )
        : std::runtime_error(std::move(message)), code(c), api_code(api), http_status(http) {
    }
};
} // namespace xmax::detail
