//
//  RoomCommandController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Room/RoomMessageDecoder.hpp"
#include "Stream/Room/RoomCommand.hpp"
#include "Foundation/Common.hpp"
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace xmax::detail {

/**
 * Coordinates command receipts, bounded RTC retries, and an independent HTTP copy.
 */
class RoomCommandController {

  public:

    /**
     * Enqueues one independently cancellable complete RTC transmission.
     */
    using Send = std::function<std::shared_future<void>(const json&, Cancel)>;

    /**
     * Relays one frozen command through HTTP without confirming generation readiness.
     */
    using Relay = std::function<void(const json&, const Cancel&)>;

    /**
     * The monotonic scheduler clock, injectable for deterministic tests.
     */
    using Now = std::function<Clock::time_point()>;

    /**
     * A logical command's identifier and first successful RTC-send completion.
     */
    struct Ticket {

        /**
         * The identifier shared by RTC attempts and the HTTP copy.
         */
        std::string xid;

        /**
         * Completes on RTC submission, initial failure, or cancellation; ACK does not complete it.
         */
        std::shared_future<void> completion;
    };

    /**
     * Creates a room-scoped scheduler; sender and relay must outlive close.
     */
    RoomCommandController(
        Send send,
        Relay relay,
        std::string user,
        std::string bot,
        Now now = [] { return Clock::now(); }
    );

    /**
     * Cancels and drains all native sends and HTTP copies.
     */
    ~RoomCommandController();

    /**
     * Freezes a command and cancels older commands of the same task and kind; start replaces all.
     * The cancellation predicate is retained and may run on background workers until task cleanup.
     */
    Ticket submit(
        json message,
        std::string task,
        Cancel cancelled
    );

    /**
     * Cancels unfinished commands for one task, or every task when empty.
     */
    void cancel(
        const std::string& task = {}
    );

    /**
     * Cancels one caller's command when its wait is interrupted.
     */
    void cancel_xid(
        const std::string& xid
    );

    /**
     * Accepts receipts only from the current result user and for the local recipient.
     */
    void receive(
        const std::string& sender,
        const std::string& data
    );

    /**
     * Permanently invalidates this room and waits for cancellable work to drain.
     */
    void close();

  private:

    /**
     * The logical command whose state is owned by this controller.
     */
    using Command = RoomCommand;

    /**
     * One native attempt belonging to a logical command.
     */
    using Attempt = Command::Attempt;

    /**
     * An RTC attempt prepared under the mutex and launched after releasing it.
     */
    using Launch = std::pair<std::shared_ptr<Command>, std::shared_ptr<Attempt>>;

    /**
     * The native complete-message submission function.
     */
    Send send_;

    /**
     * The independent HTTP copy operation.
     */
    Relay relay_;

    /**
     * The monotonic clock used to calculate retry deadlines.
     */
    Now now_;

    /**
     * The expected local receipt recipient.
     */
    std::string user_;

    /**
     * The result account authorized to acknowledge commands.
     */
    std::string bot_;

    /**
     * Protects command state and receipt reassembly.
     */
    std::mutex mutex_;

    /**
     * Wakes admission and shutdown; timers are checked at bounded intervals.
     */
    std::condition_variable wake_;

    /**
     * The room's active and draining logical commands, in initial-send order.
     */
    std::vector<std::shared_ptr<Command>> commands_;

    /**
     * The receipt fragment reassembler, scoped to this room.
     */
    RoomMessageDecoder decoder_;

    /**
     * Rejects traffic after room invalidation.
     */
    bool closed_ = false;

    /**
     * Serializes repeated close calls.
     */
    std::mutex close_mutex_;

    /**
     * Owns timer scheduling and async outcome collection.
     */
    std::thread worker_;

    /**
     * Advances retries and collects outcomes without blocking on transport.
     */
    void run();

    /**
     * Collects completed native sends and HTTP copies while holding the controller mutex.
     */
    void collect_transmissions_locked(
        const std::shared_ptr<Command>& command
    );

    /**
     * Applies one completed RTC outcome without retracting an earlier successful send.
     */
    void finish_attempt_locked(
        const std::shared_ptr<Command>& command,
        const Attempt& attempt
    );

    /**
     * Prepares due retries and preserves initial-send ordering while holding the controller mutex.
     */
    void schedule_locked(
        const std::shared_ptr<Command>& command,
        bool preceding_settled,
        std::vector<Launch>& launches,
        std::vector<std::shared_ptr<Command>>& relays
    );

    /**
     * Starts one independent HTTP copy without holding controller locks.
     */
    void launch_relay(
        const std::shared_ptr<Command>& command
    );

    /**
     * Queues one native RTC attempt without holding controller locks.
     */
    void launch_attempt(
        const Launch& launch
    );

    /**
     * Completes cancellation exactly once while retaining work until it drains.
     */
    void cancel_locked(
        const std::shared_ptr<Command>& command,
        std::exception_ptr error = {}
    );

    /**
     * Stops redundant work while retaining an unresolved original send and HTTP copy.
     */
    void stop_retries_locked(
        Command& command
    );
};
} // namespace xmax::detail
