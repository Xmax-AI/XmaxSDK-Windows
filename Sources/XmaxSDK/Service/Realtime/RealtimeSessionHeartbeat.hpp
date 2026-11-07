//
//  RealtimeSessionHeartbeat.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Network/ApiService.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace xmax::detail {

/**
 * Owns one session's periodic HTTP worker and startup-grace request state.
 * The lifecycle owner cancels it before session closure and drains it before releasing dependencies.
 */
class RealtimeSessionHeartbeat {

  public:

    /**
     * Starts an immediate heartbeat and subsequent periodic requests for a validated session.
     * Failure callbacks run on this worker and must enqueue cleanup rather than block it.
     *
     * @param[in] api The authenticated service retained until the worker has stopped.
     * @param[in] session_id The immutable session identity used by every request.
     * @param[in] first_grace_seconds The nonnegative startup duration, rounded up to whole seconds.
     * @param[in] interval The positive delay after each successful heartbeat.
     * @param[in] failure The receiver for one current-session failure.
     */
    RealtimeSessionHeartbeat(
        std::shared_ptr<ApiService> api,
        std::string session_id,
        int64_t first_grace_seconds,
        std::chrono::milliseconds interval,
        std::function<void(Error)> failure
    );

    /**
     * Cancels pending waits and drains the worker before destroying request state.
     */
    ~RealtimeSessionHeartbeat();

    /**
     * Invalidates request outcomes and wakes a periodic wait without waiting for native HTTP.
     */
    void cancel();

    /**
     * Sends one heartbeat, validating session status and preserving a server closure reason.
     *
     * @param[in] api The authenticated API service for the realtime environment.
     * @param[in] session_id The active session identifier.
     * @param[in] cancelled The cooperative request-cancellation predicate.
     * @param[in] grace The first request's startup duration; absence omits the request body.
     */
    static void request(
        const ApiService& api,
        const std::string& session_id,
        const Cancel& cancelled,
        std::optional<int64_t> grace
    );

  private:

    /**
     * The authenticated service retained by this worker.
     */
    std::shared_ptr<ApiService> api_;

    /**
     * The session identity frozen before the worker starts.
     */
    std::string session_id_;

    /**
     * The startup duration sent with the immediate first request only, in seconds.
     */
    int64_t first_grace_seconds_;

    /**
     * The interval between a successful response and the next request.
     */
    std::chrono::milliseconds interval_;

    /**
     * The receiver that forwards a heartbeat failure to lifecycle cleanup.
     */
    std::function<void(Error)> failure_;

    /**
     * Rejects delayed responses and failure delivery after session cancellation.
     */
    std::atomic<bool> cancelled_{false};

    /**
     * Protects the interruptible periodic wait.
     */
    std::mutex wait_mutex_;

    /**
     * Wakes the worker when its session is cancelled.
     */
    std::condition_variable wake_;

    /**
     * The thread drained by destruction before its request state is released.
     */
    std::thread worker_;

    /**
     * Maintains the first-request body and periodic wait on the worker thread.
     */
    void run();

    /**
     * Forwards a failure only while this heartbeat still belongs to the active session.
     */
    void report_failure(
        Error error
    );
};
} // namespace xmax::detail
