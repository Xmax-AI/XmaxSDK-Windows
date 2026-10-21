//
//  RealtimeErrorHandler.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include <algorithm>
#include <cstring>
#include <optional>

namespace xmax::detail {

/**
 * 保存当前实时状态的业务原因与原始错误，统一生成已脱敏的错误快照。
 * 本组件由生命周期线程访问，调用方负责过滤过期媒体和连接批次。
 */
class RealtimeErrorHandler {

  public:

    /**
     * 开始新的正常操作，清除上次失败原因。
     *
     * @param[in] reason 正常停止或显示方向变化等业务原因。
     */
    void reset(
        xmax_realtime_reason reason = XMAX_REASON_NORMAL
    ) {
        reason_ = reason;
        error_.reset();
    }

    /**
     * 保存操作失败的完整错误，不覆盖服务端业务码与 HTTP 状态码。
     *
     * @param[in] error 当前操作的原始 SDK 错误。
     */
    void record(
        Error error
    ) {
        reason_ = XMAX_REASON_FAILURE;
        error_ = std::move(error);
    }

    /**
     * 读取当前状态的业务原因。
     */
    xmax_realtime_reason reason() const {
        return reason_;
    }

    /**
     * 获取关联错误的借用指针；没有失败原因时为空，下一次修改后失效。
     */
    const Error* error() const {
        return error_ ? &*error_ : nullptr;
    }

    /**
     * 生成可跨 C ABI 返回的已脱敏错误详情副本。
     */
    xmax_error_info details() const {
        xmax_error_info info{};
        info.struct_size = sizeof(info);
        if (!error_)
            return info;
        info.code = error_->code;
        info.api_code = error_->api_code;
        info.http_status = error_->http_status;
        const auto message = XmaxLogger::redact(error_->what());
        auto length = std::min(message.size(), sizeof(info.message) - 1);
        while (length && length < message.size() &&
               (static_cast<unsigned char>(message[length]) & 0xc0) == 0x80)
            --length;
        std::memcpy(info.message, message.data(), length);
        return info;
    }

  private:

    /**
     * 最近一次有效操作的业务原因。
     */
    xmax_realtime_reason reason_ = XMAX_REASON_NORMAL;

    /**
     * 与当前状态关联的原始错误。
     */
    std::optional<Error> error_;
};
} // namespace xmax::detail
