//
//  RoomCommand.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include "Foundation/Json.hpp"
#include <future>

namespace xmax::detail {

/**
 * Frozen command data and owned in-flight work, protected by the room command controller's mutex.
 */
struct RoomCommand {

    /**
     * One complete RTC attempt and the cancellation flag for its queued fragments.
     */
    struct Attempt {

        /**
         * Whether this is the original transmission.
         */
        bool initial;

        /**
         * Stops queued fragments independently of other attempts.
         */
        std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);

        /**
         * The complete native-transmission outcome.
         */
        std::shared_future<void> result;
    };

    /**
     * The identifier shared by all RTC attempts and the HTTP copy.
     */
    std::string xid;

    /**
     * The frozen generation control event name.
     */
    std::string kind;

    /**
     * The owning generation task.
     */
    std::string task;

    /**
     * The immutable full JSON payload, including xid.
     */
    json message;

    /**
     * Rejects superseded lifecycle operations.
     */
    Cancel validity;

    /**
     * Cancels RTC and HTTP together at task boundaries.
     */
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);

    /**
     * Completes once on successful RTC submission, initial failure, or cancellation.
     */
    std::promise<void> completion;

    /**
     * The caller-visible outcome, independent of ACK and HTTP receipt timing.
     */
    std::shared_future<void> result = completion.get_future().share();

    /**
     * Whether the initial command has entered the scheduler.
     */
    bool begun = false;

    /**
     * Whether any complete RTC attempt has succeeded.
     */
    bool sent = false;

    /**
     * Whether the caller-visible promise has already completed.
     */
    bool settled = false;

    /**
     * Whether redundant attempts can still be scheduled.
     */
    bool retrying = true;

    /**
     * The monotonic time at which the initial attempt was scheduled.
     */
    Clock::time_point began_at{};

    /**
     * The number of RTC attempts already scheduled for this command.
     */
    size_t next = 0;

    /**
     * Submitted native attempts, retained through cancellation.
     */
    std::vector<std::shared_ptr<Attempt>> attempts;

    /**
     * Whether an HTTP copy is being launched or awaited.
     */
    bool relay_active = false;

    /**
     * The independently cancellable HTTP task.
     */
    std::future<void> relay;
};
} // namespace xmax::detail
