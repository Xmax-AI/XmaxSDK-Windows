//
//  Common.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Errors/Error.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <xmax/xmax.h>

namespace xmax::detail {

/**
 * The monotonic clock for timeouts, cancellation, and media timestamps, unaffected by wall-clock
 * adjustments.
 */
using Clock = std::chrono::steady_clock;

/**
 * The cancellation callback; true requests termination of the operation.
 */
using Cancel = std::function<bool()>;

/**
 * Throws the specified SDK error when a required condition fails.
 *
 * @param[in] value The required condition; false throws the specified SDK error.
 * @param[in] code The SDK error code to throw if the condition fails.
 * @param[in] message The diagnostic message retained in the exception if the condition fails.
 */
inline void require(
    bool value,
    xmax_result code,
    const std::string& message
) {
    if (!value)
        throw Error(
            code,
            message
        );
}
} // namespace xmax::detail
