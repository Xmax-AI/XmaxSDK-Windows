//
//  TrajectoryRegistry.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * Retains short-lived task trajectories for native effects and custom rendering.
 */
class TrajectoryRegistry {

  public:

    /**
     * Stores a sample with an expiry of 600 milliseconds.
     *
     * @param[in] points The normalized trajectory, containing at most 256 points.
     */
    void update(
        std::vector<xmax_point> points
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        points_ = std::move(points);
        expires_ = Clock::now() + std::chrono::milliseconds(600);
    }

    /**
     * Returns an unexpired trajectory copy, or an empty collection if none is valid.
     */
    std::vector<xmax_point> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return Clock::now() < expires_ ? points_ : std::vector<xmax_point>{};
    }

    /**
     * Removes trajectories immediately when a task stops or changes.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        points_.clear();
        expires_ = {};
    }

  private:

    /**
     * The mutex protecting the trajectory cache.
     */
    mutable std::mutex mutex_;

    /**
     * The current task's latest normalized trajectory.
     */
    std::vector<xmax_point> points_;

    /**
     * The trajectory display deadline on the monotonic clock.
     */
    Clock::time_point expires_{};
};
} // namespace xmax::detail
