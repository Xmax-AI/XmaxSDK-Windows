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
 * 封装失败的 SDK 结果码及诊断信息，供 C++ 接入方以异常方式处理错误。
 */
class error : public std::runtime_error {

  public:

    /**
     * 使用 SDK 错误码与诊断文本构造异常。
     *
     * @param[in] code 本异常保存的 SDK 结果码。
     * @param[in] message 以 NUL 结尾的诊断文本，构造时复制，不能为空指针。
     * @param[in] detail 本次失败的结构化详情。
     */
    error(
        xmax_result code,
        const char* message,
        xmax_error_info detail = {}
    )
        : std::runtime_error(message), code_(code), detail_(detail) {
    }

    /**
     * 读取构造异常时保存的 SDK 错误码。
     */
    xmax_result code() const noexcept {
        return code_;
    }

    /**
     * 读取服务端业务错误码；0 表示未提供。
     */
    int32_t api_code() const noexcept {
        return detail_.api_code;
    }

    /**
     * 读取 HTTP 响应状态码；0 表示未获得响应。
     */
    int32_t http_status() const noexcept {
        return detail_.http_status;
    }

  private:

    /**
     * 构造异常时保存的原始 SDK 错误码。
     */
    xmax_result code_;

    /**
     * 本次失败的服务端业务码及 HTTP 响应信息。
     */
    xmax_error_info detail_;
};

/**
 * 将负数 C 接口结果转换为当前线程上的 C++ SDK 异常。
 *
 * @param[in] result 待处理的 C 接口结果；XMAX_OK 和 XMAX_NO_EVENT 不抛出异常。
 */
inline void check(
    xmax_result result
) {
    if (result < 0) {
        xmax_error_info info{};
        info.struct_size = sizeof(info);
        xmax_last_error_info(&info);
        throw error(result, xmax_last_error(), info);
    }
}
} // namespace xmax
