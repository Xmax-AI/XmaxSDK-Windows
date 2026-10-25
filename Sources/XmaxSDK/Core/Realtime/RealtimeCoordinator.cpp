//
//  RealtimeCoordinator.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/RealtimeCoordinator.hpp"
#include "Foundation/Logging/RtcStatsLogger.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {

/**
 * Starts the serialized lifecycle worker and installs heartbeat and asynchronous failure callbacks.
 *
 * @param[in] tick The lifecycle heartbeat callback, invoked on the worker thread.
 * @param[in] failure The asynchronous failure receiver, invoked on the worker thread.
 */
RealtimeCoordinator::RealtimeCoordinator(
    std::function<void()> tick,
    std::function<void(Error)> failure
)
    : tick_(std::move(tick)), failure_(std::move(failure)) {
    worker_ = std::thread([this] { run(); });
}

/**
 * Drains and stops the lifecycle worker before releasing its resources.
 */
RealtimeCoordinator::~RealtimeCoordinator() {
    shutdown();
}

/**
 * Stops accepting work, drains the queue, and waits for the worker to exit.
 */
void RealtimeCoordinator::shutdown() {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        exiting_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

/**
 * Queues asynchronous lifecycle work; no new work is accepted after shutdown starts.
 *
 * @param[in] fn The queued operation; failures are reported through the error callback.
 */
void RealtimeCoordinator::enqueue(
    std::function<void()> fn
) {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        work_.push_back(std::move(fn));
    }
    wake_.notify_one();
}

/**
 * Runs work on the lifecycle queue and returns completion or exceptions to the calling thread.
 *
 * @param[in] fn The lifecycle operation; synchronous execution propagates exceptions to the calling thread.
 */
void RealtimeCoordinator::sync(
    std::function<void()> fn
) {
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
    auto result = task->get_future();
    enqueue([task] { (*task)(); });
    result.get();
}

/**
 * Serializes lifecycle work and due heartbeats, draining accepted work during shutdown.
 */
void RealtimeCoordinator::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(work_mutex_);
            wake_.wait_until(
                lock,
                deadline_,
                [this] { return exiting_ || !work_.empty(); }
            );

            if (exiting_ && work_.empty())
                return;

            if (!work_.empty()) {
                job = std::move(work_.front());
                work_.pop_front();
            }
        }
        try {
            if (job)
                job();
            if (Clock::now() >= deadline_)
                tick_();
        } catch (...) {
            deadline_ = Clock::time_point::max();
            // Keep asynchronous callback failures from terminating the lifecycle worker.
            try {
                if (failure_)
                    failure_(Error(
                        XMAX_INTERNAL_ERROR,
                        "An asynchronous SDK operation failed"
                    ));
                else {
                    xmax_event event{};
                    event.type = XMAX_EVENT_ERROR;
                    event.code = XMAX_INTERNAL_ERROR;
                    emit(event);
                }
            } catch (...) {
            }
        }
    }
}

/**
 * Creates a thread-safe cancellation predicate for a revision.
 *
 * @param[in] epoch The cancellation revision captured when the operation starts, used to reject stale work.
 *
 * @return A callable returning true when the revision has been cancelled.
 */
Cancel RealtimeCoordinator::cancellation(
    uint64_t epoch
) {
    return [this, epoch] { return epoch_.load() != epoch; };
}

/**
 * Queues an application event and logs it according to the current configuration; the queue is bounded.
 *
 * @param[in] event The event to enqueue.
 * @param[in] reason The state-transition reason for the current state or error.
 * @param[in] error Associated error details, copied during the call.
 */
void RealtimeCoordinator::emit(
    xmax_event event,
    xmax_realtime_reason reason,
    const Error* error
) {
    RtcStatsLogger::event(event);

    if (event.type == XMAX_EVENT_ERROR)
        XmaxLogger::write(
            LogCategory::Realtime,
            XMAX_LOG_ERROR,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "实时操作失败 (Realtime Operation Failed)",
                    "Realtime Operation Failed"
                ) +
                       "\n├─ code=" + std::to_string(event.code) + ", session=" + event.session_id +
                       ", task=" + event.task_id + "\n└─ " + std::string(event.message);
            }
        );

    event.struct_size = sizeof(event);

    std::lock_guard<std::mutex> lock(event_mutex_);

    if (events_.size() >= 256) {
        auto old = std::find_if(
            events_.begin(),
            events_.end(),
            [](const auto& e) {
                return e.event.type == XMAX_EVENT_STATISTICS;
            }
        );
        if (old != events_.end())
            events_.erase(old);
        else
            events_.pop_front();
    }

    xmax_event_info info{};
    info.struct_size = sizeof(info);
    info.event = event;
    info.reason = reason;
    info.error.struct_size = sizeof(info.error);
    if (error) {
        info.error.code = error->code;
        info.error.api_code = error->api_code;
        info.error.http_status = error->http_status;
        const auto safe = XmaxLogger::redact(error->what());
        auto length = std::min(safe.size(), sizeof(info.error.message) - 1);
        while (length && length < safe.size() && (static_cast<unsigned char>(safe[length]) & 0xc0) == 0x80)
            --length;
        std::memcpy(
            info.error.message,
            safe.data(),
            length
        );
    }
    events_.push_back(info);
}

/**
 * Acquires the oldest pending application event without blocking.
 *
 * @param[out] event Receives the oldest pending event; unchanged if the queue is empty.
 *
 * @return True if an event was retrieved; false if the queue is empty.
 */
bool RealtimeCoordinator::poll(
    xmax_event& event
) {
    xmax_event_info info{};
    if (!poll(info))
        return false;
    event = info.event;
    return true;
}

/**
 * Acquires the oldest event with state reason and error details without blocking.
 *
 * @param[out] info Receives the event snapshot; unchanged if the queue is empty.
 *
 * @return Whether an event was retrieved.
 */
bool RealtimeCoordinator::poll(
    xmax_event_info& info
) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.empty())
        return false;
    info = events_.front();
    events_.pop_front();
    return true;
}
} // namespace xmax::detail
