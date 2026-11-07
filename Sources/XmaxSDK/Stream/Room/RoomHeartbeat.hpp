//
//  RoomHeartbeat.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Room/RoomMessageSender.hpp"

namespace xmax::detail {

/** Schedules RTC room keepalives independently of generation and lifecycle work. */
class RoomHeartbeat {

  public:

    /** Cancels the current cycle before releasing its sender. */
    ~RoomHeartbeat();

    /**
     * Starts a cycle for the joined RTC backend with a frozen room identity.
     *
     * @param[in,out] sender The room sender, which outlives the cycle.
     * @param[in] rtc The RTC backend retained until the cycle stops.
     * @param[in] message The frozen room heartbeat payload.
     * @param[in] interval The positive delay before each send.
     */
    void start(
        RoomMessageSender& sender,
        std::shared_ptr<Rtc> rtc,
        json message,
        std::chrono::milliseconds interval
    );

    /** Stops the cycle and drains the scheduler before the room leaves. */
    void stop() noexcept;

  private:

    /** Serializes sleep and cancellation of the active cycle. */
    std::mutex mutex_;

    /** Wakes the scheduler when the room closes. */
    std::condition_variable wake_;

    /** Shared by the scheduler and its queued sends. */
    std::shared_ptr<std::atomic<bool>> cancelled_;

    /** Sleeps and submits one heartbeat at a time. */
    std::thread worker_;
};

} // namespace xmax::detail
