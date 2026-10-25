//
//  error.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "xmax.h"
#include <stdexcept>

namespace xmax {

/**
 * An SDK result code and diagnostic exposed as an exception to C++ callers.
 */
class error : public std::runtime_error {

  public:

    /**
     * Creates an exception with an SDK result code and diagnostic text.
     *
     * @param[in] code The SDK result code retained by the exception.
     * @param[in] message The non-null NUL-terminated diagnostic, copied during construction.
     * @param[in] detail The structured details of this failure.
     */
    error(
        xmax_result code,
        const char* message,
        xmax_error_info detail = {}
    )
        : std::runtime_error(message), code_(code), detail_(detail) {
    }

    /**
     * Returns the SDK result code retained at construction.
     */
    xmax_result code() const noexcept {
        return code_;
    }

    /**
     * Returns the service error code, or 0 if absent.
     */
    int32_t api_code() const noexcept {
        return detail_.api_code;
    }

    /**
     * Returns the HTTP response status, or 0 if no response was received.
     */
    int32_t http_status() const noexcept {
        return detail_.http_status;
    }

  private:

    /**
     * The original SDK error code retained at construction.
     */
    xmax_result code_;

    /**
     * The service error code and HTTP response information for this failure.
     */
    xmax_error_info detail_;
};

/**
 * Translates negative C interface results to C++ SDK exceptions on the calling thread.
 *
 * @param[in] result The C interface result; XMAX_OK and XMAX_NO_EVENT do not throw.
 */
inline void check(
    xmax_result result
) {
    if (result < 0) {
        xmax_error_info info{};
        info.struct_size = sizeof(info);
        xmax_last_error_info(&info);
        throw error(
            result,
            xmax_last_error(),
            info
        );
    }
}
} // namespace xmax
