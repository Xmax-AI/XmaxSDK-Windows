//
//  RoomMessageSender.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/RTC/RtcPlatform.hpp"
#include "Stream/Room/MessageCodec.hpp"
#include <deque>

namespace xmax::detail {

/**
 * Fragments and sends room messages under a sliding budget with cancellation and keepalives while waiting.
 */
class RoomMessageSender {

  public:

    /**
     * Sends UTF-8 fragments under a sliding budget, checking cancellation and maintaining keepalives while
     * waiting.
     *
     * @param[in] rtc A borrowed connected RTC adapter that can send signaling fragments.
     * @param[in] message The room JSON message to serialize, fragment, and send.
     * @param[in] cancelled A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] maintain The maintenance callback invoked while waiting or throttled; empty omits
     * maintenance.
     */
    void
    send(
        Rtc& rtc,
        const json& message,
        const Cancel& cancelled,
        const std::function<void()>& maintain = {}
    );

    /**
     * Clears send-budget history after releasing the room.
     */
    void reset() {
        sent_.clear();
    }

  private:

    /**
     * The send timestamps and byte counts used for the sliding signaling budget.
     */
    std::deque<std::pair<Clock::time_point, size_t>> sent_;
};
} // namespace xmax::detail
