//
//  InteractionController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include "Media/Interaction/InteractionCoordinateMapper.hpp"
#include <mutex>
#include <optional>

namespace xmax::detail {

/**
 * Coalesces pending trajectory samples without waiting for RTC sends on the submitting thread.
 */
class InteractionController {

  public:

    /**
     * The lifecycle revision and points belonging to one sample.
     */
    struct Sample {

        /**
         * The connection cancellation revision captured at submission, rejecting older connections.
         */
        uint64_t epoch;

        /**
         * The task captured at submission; never reused for a new task after camera switching.
         */
        std::string task;

        /**
         * The mapped normalized points.
         */
        std::vector<xmax_point> points;
    };

    /**
     * Replaces unsent samples and reserves at most one pending send task.
     *
     * @param[in] sample The copied sample carrying its task identity.
     *
     * @return True when the caller should schedule one send task.
     */
    bool submit(
        Sample sample
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::move(sample);
        if (scheduled_)
            return false;
        scheduled_ = true;
        return true;
    }

    /**
     * Takes the latest sample; no sample releases the reservation so later submissions can schedule again.
     */
    std::optional<Sample> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto value = std::exchange(
            pending_,
            {}
        );
        if (!value)
            scheduled_ = false;
        return value;
    }

    /**
     * Clears unsent samples; an already reserved task remains responsible for releasing its reservation.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
    }

  private:

    /**
     * The mutex protecting the latest sample and task reservation.
     */
    std::mutex mutex_;

    /**
     * The latest unsent sample, which may replace older samples.
     */
    std::optional<Sample> pending_;

    /**
     * Whether a send task is already running or queued.
     */
    bool scheduled_ = false;
};
} // namespace xmax::detail
