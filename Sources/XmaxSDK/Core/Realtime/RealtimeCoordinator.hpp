//
//  RealtimeCoordinator.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace xmax::detail {

/**
 * Serializes realtime lifecycle work, cancellation revisions, heartbeat deadlines, and bounded event
 * delivery.
 */
class RealtimeCoordinator {

  public:

    /**
     * Starts the serialized lifecycle worker and installs heartbeat and asynchronous failure callbacks.
     *
     * @param[in] tick The lifecycle heartbeat callback, invoked on the worker thread.
     * @param[in] failure The asynchronous failure receiver, invoked on the worker thread.
     */
    explicit RealtimeCoordinator(
        std::function<void()> tick,
        std::function<void(Error)> failure = {}
    );

    /**
     * Drains and stops the lifecycle worker before releasing its resources.
     */
    ~RealtimeCoordinator();

    /**
     * Stops accepting work, drains the queue, and waits for the worker to exit.
     */
    void shutdown();

    /**
     * Runs work on the lifecycle queue and returns completion or exceptions to the calling thread.
     *
     * @param[in] fn The lifecycle operation; synchronous execution propagates exceptions to the calling
     * thread.
     */
    void sync(
        std::function<void()> fn
    );

    /**
     * Queues asynchronous lifecycle work; no new work is accepted after shutdown starts.
     *
     * @param[in] fn The queued operation; failures are reported through the error callback.
     */
    void enqueue(
        std::function<void()> fn
    );

    /**
     * Returns the cancellation revision atomically.
     */
    uint64_t epoch() const {
        return epoch_.load();
    }

    /**
     * Invalidates earlier operations and returns the new cancellation revision.
     */
    uint64_t invalidate() {
        return ++epoch_;
    }

    /**
     * Creates a thread-safe cancellation predicate for a revision.
     *
     * @param[in] epoch The cancellation revision captured when the operation starts, used to reject stale
     * work.
     *
     * @return A callable returning true when the revision has been cancelled.
     */
    Cancel cancellation(
        uint64_t epoch
    );

    /**
     * Returns lifecycle state without waiting for queued work.
     */
    xmax_state state() const {
        return state_.load();
    }

    /**
     * Publishes a lifecycle state atomically.
     *
     * @param[in] value The new realtime lifecycle state.
     */
    void state(
        xmax_state value
    ) {
        state_ = value;
    }

    /**
     * Queues an application event and logs it according to the current configuration; the queue is bounded.
     *
     * @param[in] event The event to enqueue.
     * @param[in] reason The state-transition reason for the current state or error.
     * @param[in] error Associated error details, copied during the call.
     */
    void
    emit(
        xmax_event event,
        xmax_realtime_reason reason = XMAX_REASON_NORMAL,
        const Error* error = nullptr
    );

    /**
     * Acquires the oldest pending application event without blocking.
     *
     * @param[out] event Receives the oldest pending event; unchanged if the queue is empty.
     *
     * @return True if an event was retrieved; false if the queue is empty.
     */
    bool poll(
        xmax_event& event
    );

    /**
     * Acquires the oldest event with state reason and error details without blocking.
     *
     * @param[out] info Receives the event snapshot; unchanged if the queue is empty.
     *
     * @return Whether an event was retrieved; shares the legacy polling queue.
     */
    bool poll(
        xmax_event_info& info
    );

    /**
     * Updates the next heartbeat's monotonic deadline.
     *
     * @param[in] value The next heartbeat deadline on the monotonic clock.
     */
    void deadline(
        Clock::time_point value
    ) {
        deadline_ = value;
    }

    /**
     * Returns the next heartbeat's monotonic deadline.
     */
    Clock::time_point deadline() const {
        return deadline_;
    }

  private:

    /**
     * The due-heartbeat handler, invoked on the lifecycle worker.
     */
    std::function<void()> tick_;

    /**
     * The asynchronous lifecycle failure callback, invoked on the worker.
     */
    std::function<void(Error)> failure_;

    /**
     * The worker that serializes lifecycle operations.
     */
    std::thread worker_;

    /**
     * The mutex protecting pending work and shutdown state.
     */
    std::mutex work_mutex_;

    /**
     * The mutex protecting the application event queue.
     */
    std::mutex event_mutex_;

    /**
     * Notifies the worker of new work, shutdown, or deadline changes.
     */
    std::condition_variable wake_;

    /**
     * The lifecycle operations in submission order.
     */
    std::deque<std::function<void()>> work_;

    /**
     * The bounded application event queue for nonblocking consumption.
     */
    std::deque<xmax_event_info> events_;

    /**
     * Whether shutdown has started, protected by the work-queue mutex.
     */
    bool exiting_ = false;

    /**
     * The cancellation revision; incrementing it invalidates earlier work.
     */
    std::atomic<uint64_t> epoch_{0};

    /**
     * The lifecycle state, read atomically across threads.
     */
    std::atomic<xmax_state> state_{XMAX_IDLE};

    /**
     * The next heartbeat deadline on the monotonic clock.
     */
    Clock::time_point deadline_ = Clock::time_point::max();

    /**
     * Serializes lifecycle work and due heartbeats, draining accepted work during shutdown.
     */
    void run();
};
} // namespace xmax::detail
