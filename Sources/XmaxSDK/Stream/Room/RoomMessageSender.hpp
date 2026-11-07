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
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace xmax::detail {
/**
 * Serializes complete RTC sends under one sliding budget, with cancellable queued fragments.
 */
class RoomMessageSender {

  public:

    /**
     * Starts the sender worker without opening native RTC resources.
     */
    RoomMessageSender();

    /**
     * Cancels and drains queued sends before joining the worker.
     */
    ~RoomMessageSender();

    /**
     * Queues a complete message, retaining the RTC adapter until all its fragments finish.
     */
    std::shared_future<void> enqueue(
        std::shared_ptr<Rtc> rtc,
        const json& message,
        Cancel cancelled
    );

    /**
     * Cancels and drains all room traffic, then clears the send budget for reuse.
     */
    void reset();

  private:

    /**
     * One ordered transmission and the room revision that owns it.
     */
    struct Transmission {

        /**
         * The native adapter retained until completion.
         */
        std::shared_ptr<Rtc> rtc;

        /**
         * The frozen UTF-8 provider packets.
         */
        std::vector<std::string> packets;

        /**
         * The command or caller's cooperative cancellation predicate.
         */
        Cancel cancelled;

        /**
         * The room-close flag shared with reset.
         */
        std::shared_ptr<std::atomic<bool>> room;

        /**
         * The completion of the entire transmission.
         */
        std::promise<void> completion;
    };

    /**
     * Protects admission, the FIFO, and worker draining.
     */
    std::mutex mutex_;

    /**
     * Wakes queued work and reset waiters.
     */
    std::condition_variable wake_;

    /**
     * The pending complete-message transmissions.
     */
    std::deque<std::shared_ptr<Transmission>> queue_;

    /**
     * Invalidates all traffic admitted before a room reset.
     */
    std::shared_ptr<std::atomic<bool>> room_ = std::make_shared<std::atomic<bool>>(false);

    /**
     * Whether a transmission is executing.
     */
    bool busy_ = false;

    /**
     * Whether reset temporarily rejects new sends.
     */
    bool resetting_ = false;

    /**
     * Whether destruction has stopped admission.
     */
    bool stopped_ = false;

    /**
     * The send times and byte counts, owned by the worker except while fully drained.
     */
    std::deque<std::pair<Clock::time_point, size_t>> sent_;

    /**
     * The single native signaling worker.
     */
    std::thread worker_;

    /**
     * Sends queued messages in order and completes each transmission exactly once.
     */
    void run();

    /**
     * Sends one transmission while checking cancellation between fragments and rate-limit waits.
     */
    void transmit(
        const Transmission& value
    );
};
} // namespace xmax::detail
