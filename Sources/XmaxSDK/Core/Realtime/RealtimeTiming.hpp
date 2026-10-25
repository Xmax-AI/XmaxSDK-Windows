//
//  RealtimeTiming.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <algorithm>

namespace xmax::detail {

/**
 * Tracks session creation, RTC join, local publication, and first-result startup durations. Lifecycle code
 * updates stages; RTC callbacks atomically update their publication revision only.
 */
class RealtimeTiming {

  public:

    /**
     * A single publication record shared between RTC callbacks and lifecycle execution.
     */
    struct Publication {

        /**
         * The first actual publication time in monotonic microseconds; -1 until notified.
         */
        std::atomic<int64_t> first_us{-1};

        /**
         * The start of local frame delivery in monotonic microseconds; -1 until started.
         */
        int64_t started_us = -1;
    };

    /**
     * The injectable monotonic clock used to measure stage durations.
     */
    using Now = std::function<Clock::time_point()>;

    /**
     * Retains a monotonic clock whose readings use one time base.
     *
     * @param[in] now The monotonic clock callable, safe to invoke on the lifecycle thread.
     */
    explicit RealtimeTiming(
        Now now = [] { return Clock::now(); }
    )
        : now_(std::move(now)) {
    }

    /**
     * Starts connection timing and clears previous stage values.
     */
    void begin() {
        started_ = now_();
        snapshot_ = {};
        snapshot_.struct_size = sizeof(snapshot_);
        snapshot_.type = XMAX_EVENT_TIMING;
        snapshot_.session_ms = snapshot_.rtc_join_ms = snapshot_.publish_ms = snapshot_.first_frame_ms =
            snapshot_.total_ms = -1;
        publication_.reset();
    }

    /**
     * Records completion of server-session creation.
     */
    void finish_session() {
        snapshot_.session_ms = elapsed(
            started_,
            now_()
        );
    }

    /**
     * Starts RTC join timing and allocates a publication revision.
     */
    void begin_join() {
        joining_ = now_();
        publication_ = std::make_shared<Publication>();
    }

    /**
     * Starts local frame delivery timing for the first actual publication.
     */
    void begin_publication() {
        if (publication_)
            publication_->started_us = microseconds(now_());
    }

    /**
     * Records completion of RTC join and local delivery setup.
     */
    void finish_join() {
        snapshot_.rtc_join_ms = elapsed(
            joining_,
            now_()
        );
    }

    /**
     * Starts timing generation signaling and the task's first result frame.
     */
    void begin_signal() {
        signalling_ = now_();
    }

    /**
     * Records first-result and total startup durations and merges publication notifications.
     */
    void finish_signal() {
        const auto now = now_();
        snapshot_.first_frame_ms = elapsed(
            signalling_,
            now
        );
        snapshot_.total_ms = elapsed(
            started_,
            now
        );
        update_publication();
    }

    /**
     * Returns the publication revision retained by this connection's RTC callbacks.
     */
    std::shared_ptr<Publication> publication() const {
        return publication_;
    }

    /**
     * Atomically records the first publication notification without overwriting its timestamp.
     *
     * @param[in] publication The publication revision retained when the callback was created.
     * @param[in] at The notification's monotonic timestamp.
     *
     * @return True for the first notification of this revision.
     */
    static bool published(
        const std::shared_ptr<Publication>& publication,
        Clock::time_point at
    ) {
        if (!publication)
            return false;

        int64_t missing = -1;
        return publication->first_us.compare_exchange_strong(
            missing,
            microseconds(at)
        );
    }

    /**
     * Merges first-publication timing for the current revision and rejects earlier connections.
     *
     * @param[in] publication The publication revision to check.
     *
     * @return True when a valid publication duration was newly recorded.
     */
    bool update_publication(
        const std::shared_ptr<Publication>& publication
    ) {
        if (publication != publication_ || snapshot_.publish_ms >= 0)
            return false;
        return update_publication();
    }

    /**
     * Returns stage durations in milliseconds; unfinished stages are -1.
     */
    xmax_event snapshot() const {
        return snapshot_;
    }

    /**
     * Releases the publication revision so old RTC callbacks cannot affect later connection timing.
     */
    void reset_publication() {
        publication_.reset();
    }

  private:

    /**
     * The monotonic clock used for timing.
     */
    Now now_;

    /**
     * The current connection's start time.
     */
    Clock::time_point started_{};

    /**
     * The RTC join start time.
     */
    Clock::time_point joining_{};

    /**
     * The current generation signaling start time.
     */
    Clock::time_point signalling_{};

    /**
     * The latest stage-duration snapshot.
     */
    xmax_event snapshot_{};

    /**
     * The current connection's publication notification record.
     */
    std::shared_ptr<Publication> publication_;

    /**
     * Converts a monotonic timestamp to microseconds for atomic cross-thread notification.
     *
     * @param[in] at The monotonic timestamp.
     *
     * @return The microseconds elapsed since the monotonic clock's epoch.
     */
    static int64_t microseconds(
        Clock::time_point at
    ) {
        return std::chrono::duration_cast<std::chrono::microseconds>(at.time_since_epoch()).count();
    }

    /**
     * Calculates a nonnegative stage duration.
     *
     * @param[in] begin The stage start time.
     * @param[in] end The stage completion time.
     *
     * @return The nonnegative duration, in milliseconds.
     */
    static double elapsed(
        Clock::time_point begin,
        Clock::time_point end
    ) {
        return std::max(0.0, std::chrono::duration<double, std::milli>(end - begin).count());
    }

    /**
     * Merges a received first-publication notification when its start time is known.
     *
     * @return True if a valid publication duration was recorded.
     */
    bool update_publication() {
        if (!publication_ || publication_->started_us < 0)
            return false;
        const auto first = publication_->first_us.load();
        if (first < 0)
            return false;
        snapshot_.publish_ms = std::max(0.0, double(first - publication_->started_us) / 1000);
        return true;
    }
};
} // namespace xmax::detail
