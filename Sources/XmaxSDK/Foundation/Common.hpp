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
 * 用于超时、取消和媒体时间戳的单调时钟，不受系统日期校时影响。
 */
using Clock = std::chrono::steady_clock;

/**
 * 协作式取消判断回调；返回 true 表示调用方应终止当前操作。
 */
using Cancel = std::function<bool()>;

/**
 * 在条件不成立时抛出指定错误码和诊断的 SDK 异常。
 *
 * @param[in] value 必须成立的条件，false 时抛出指定 SDK 错误。
 * @param[in] code 条件不成立时抛出的 SDK 错误码。
 * @param[in] message 条件不成立时保存在异常中的诊断文本。
 */
inline void require(
    bool value,
    xmax_result code,
    const std::string& message
) {
    if (!value)
        throw Error(code, message);
}
} // namespace xmax::detail
