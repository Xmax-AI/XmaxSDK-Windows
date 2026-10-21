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
 * 携带 SDK 错误码和诊断文本的内部异常，由 C 接口转换为结果码。
 */
struct Error : std::runtime_error {

    /**
     * 本异常携带的 SDK 错误码。
     */
    xmax_result code;

    /**
     * 服务端业务错误码，0 表示未提供。
     */
    int32_t api_code = 0;

    /**
     * HTTP 响应状态码，0 表示未获得响应。
     */
    int32_t http_status = 0;

    /**
     * 使用 SDK 错误码与诊断文本构造内部异常。
     *
     * @param[in] c 本异常保存的 SDK 错误码。
     * @param[in] message 本异常保存的诊断文本，构造时复制到异常对象。
     * @param[in] api 服务端业务错误码，0 表示未提供。
     * @param[in] http HTTP 响应状态码，0 表示未获得响应。
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
