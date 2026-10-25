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
#include "Foundation/Render/TrajectoryEffect.hpp"
#include <deque>
#include <mutex>

namespace xmax::detail {

/**
 * Retains short-lived task trajectories for native effects and custom rendering.
 */
class TrajectoryRegistry {

  public:

    /**
     * Stores a sample and its fading trail for at most 64 refreshes at 60 Hz.
     *
     * @param[in] points The normalized trajectory, containing at most 256 points.
     */
    void update(
        std::vector<xmax_point> points
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        if (now >= expires_) {
            history_.clear();
            started_ = now;
        }
        points_ = std::move(points);
        if (!history_.empty() && !history_.back().points.empty() && !points_.empty())
            history_.push_back({{history_.back().points.back(), points_.front()}, now});
        history_.push_back({points_, now});
        size_t count = 0;
        for (const auto& entry : history_)
            count += entry.points.size();
        while (history_.size() > 64 || count > 1024 ||
               (!history_.empty() && now - history_.front().at > std::chrono::milliseconds(1067))) {
            count -= history_.front().points.size();
            history_.pop_front();
        }
        expires_ = now + std::chrono::milliseconds(1067);
    }

    /**
     * Returns an unexpired trajectory copy, or an empty collection if none is valid.
     */
    std::vector<xmax_point> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return Clock::now() < expires_ ? points_ : std::vector<xmax_point>{};
    }

    /**
     * Returns fading strokes and the active gesture age using one consistent clock snapshot.
     *
     * @param[out] elapsed Receives the gesture age, in seconds.
     *
     * @return The recent strokes with their individual fade ages; empty after expiry or task cleanup.
     */
    std::vector<TrajectoryStroke> effects(
        double& elapsed
    ) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        elapsed = std::chrono::duration<double>(now - started_).count();
        std::vector<TrajectoryStroke> result;
        if (now >= expires_)
            return result;
        for (const auto& entry : history_) {
            const double age = std::chrono::duration<double>(now - entry.at).count();
            if (age < 64.0 / 60)
                result.push_back({entry.points, age});
        }
        return result;
    }

    /**
     * Removes trajectories immediately when a task stops or changes.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        points_.clear();
        history_.clear();
        expires_ = {};
        started_ = {};
    }

  private:

    /**
     * A sampled stroke and its monotonic creation time.
     */
    struct Entry {

        /**
         * The normalized sampled coordinates.
         */
        std::vector<xmax_point> points;

        /**
         * The creation time used for trail fading.
         */
        Clock::time_point at;
    };

    /**
     * The recent strokes and links, bounded to 64 batches and 1024 sampled points.
     */
    std::deque<Entry> history_;

    /**
     * The current gesture's monotonic start time, retained across input batches.
     */
    Clock::time_point started_{};

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
