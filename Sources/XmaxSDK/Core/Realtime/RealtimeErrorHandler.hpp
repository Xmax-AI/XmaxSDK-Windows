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
 * Retains the state reason and original error and builds redacted error snapshots. Access is confined to the
 * lifecycle thread; callers filter stale media and connection revisions.
 */
class RealtimeErrorHandler {

  public:

    /**
     * Begins a normal operation and clears the previous failure reason.
     *
     * @param[in] reason A state-transition reason, such as normal stop or display-orientation change.
     */
    void reset(
        xmax_realtime_reason reason = XMAX_REASON_NORMAL
    ) {
        reason_ = reason;
        error_.reset();
    }

    /**
     * Retains the original failure, including service and HTTP status codes.
     *
     * @param[in] error The current operation's original SDK error.
     */
    void record(
        Error error
    ) {
        reason_ = XMAX_REASON_FAILURE;
        error_ = std::move(error);
    }

    /**
     * Returns the state-transition reason for the current state.
     */
    xmax_realtime_reason reason() const {
        return reason_;
    }

    /**
     * Returns a borrowed error pointer, or null without a failure; invalidated by the next mutation.
     */
    const Error* error() const {
        return error_ ? &*error_ : nullptr;
    }

    /**
     * Builds an owned, redacted error snapshot for the C ABI.
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
        std::memcpy(
            info.message,
            message.data(),
            length
        );
        return info;
    }

  private:

    /**
     * The state-transition reason for the latest valid operation.
     */
    xmax_realtime_reason reason_ = XMAX_REASON_NORMAL;

    /**
     * The original error associated with the current state.
     */
    std::optional<Error> error_;
};
} // namespace xmax::detail
