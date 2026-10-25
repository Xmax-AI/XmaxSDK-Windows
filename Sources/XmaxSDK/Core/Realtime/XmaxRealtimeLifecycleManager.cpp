//
//  XmaxRealtimeLifecycleManager.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeLifecycleManager.hpp"
#include "Foundation/RTC/RtcStatsLogger.hpp"
#include "Foundation/Text/Utf8.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {

/**
 * Starts the serial worker with heartbeat and asynchronous failure receivers.
 *
 * @param[in] tick The due-heartbeat handler.
 * @param[in] failure The handler for unexpected asynchronous exceptions.
 */
XmaxRealtimeLifecycleManager::XmaxRealtimeLifecycleManager(
    std::function<void()> tick,
    std::function<void(Error)> failure
) : tick_(std::move(tick)), failure_(std::move(failure)) {
    snapshot_.struct_size = sizeof(snapshot_);
    snapshot_.event.struct_size = sizeof(snapshot_.event);
    snapshot_.event.type = XMAX_EVENT_STATE;
    snapshot_.error.struct_size = sizeof(snapshot_.error);
    worker_ = std::thread([this] { worker_loop(); });
}

/**
 * Drains accepted work and stops the lifecycle worker.
 */
XmaxRealtimeLifecycleManager::~XmaxRealtimeLifecycleManager() {
    shutdown();
}

/**
 * Installs resource cleanup, shared errors, and state bookkeeping before admitting operations.
 *
 * @param[in] errors The error manager, which outlives this lifecycle manager.
 * @param[in] cleanup The resource-release handler; it must not submit blocking lifecycle work.
 * @param[in] state_handler Optional state bookkeeping performed before event delivery.
 */
void XmaxRealtimeLifecycleManager::configure(
    XmaxRealtimeErrorManager& errors,
    CleanupHandler cleanup,
    StateHandler state_handler
) {
    errors_ = &errors;
    cleanup_ = std::move(cleanup);
    state_handler_ = std::move(state_handler);
}

/**
     * Rejects results from an operation that was terminated or replaced.
     */
void XmaxRealtimeLifecycleManager::Token::ensure_current() const {
    if (is_current())
        return;
    std::lock_guard<std::mutex> lock(lease_->error_mutex);
    if (lease_->terminal_error)
        throw *lease_->terminal_error;
    throw Error(
        XMAX_CANCELLED,
        "Realtime operation was cancelled or replaced"
    );
}

/**
     * Returns whether this operation can still commit results.
     */
bool XmaxRealtimeLifecycleManager::Token::is_current() const {
    return lease_->valid.load();
}

/**
     * Sets the resources released if this operation fails after validation.
     *
     * @param[in] scope The rollback scope for this operation.
     */
void XmaxRealtimeLifecycleManager::Token::failure_scope(
    TerminationScope scope
) const {
    lease_->failure_scope = scope;
}

/**
     * Returns a cancellation predicate that retains this operation's validity record.
     */
Cancel XmaxRealtimeLifecycleManager::Token::cancellation() const {
    return [lease = lease_] { return !lease->valid.load(); };
}

/**
 * Admits a foreground operation; generation replaces earlier generation work after its cleanup.
 *
 * @param[in] kind The resource operation being requested.
 * @param[in] body The blocking work, which checks its token across resource calls.
 *
 * @note Conflicting foreground operations fail with XMAX_INVALID_STATE. Do not call from the worker.
 */
void XmaxRealtimeLifecycleManager::run(
    OperationKind kind,
    const std::function<void(const Token&)>& body
) {
    require(
        worker_.get_id() != std::this_thread::get_id(),
        XMAX_INVALID_STATE,
        "Reentrant lifecycle operation"
    );
    const auto lease = std::make_shared<Lease>(kind);
    auto task = std::make_shared<std::packaged_task<void()>>([this, lease, body] {
        const Token token(lease);
        {
            std::lock_guard<std::mutex> lock(admission_mutex_);
            executing_ = lease;
            if (lease->kind == OperationKind::Generation && state() == XMAX_GENERATING)
                lease->effective_kind = OperationKind::Configuration;
        }
        try {
            token.ensure_current();
            body(token);
            token.ensure_current();
            {
                std::lock_guard<std::mutex> lock(admission_mutex_);
                token.ensure_current();
                if (admitted_ == lease)
                    admitted_.reset();
                executing_.reset();
                if (lease->completion_state)
                    publish(std::move(*lease->completion_state));
            }
            if (lease->completion_handler)
                lease->completion_handler();
        } catch (...) {
            const auto original = std::current_exception();
            Error error(
                XMAX_INTERNAL_ERROR,
                "Realtime operation failed"
            );
            try {
                std::rethrow_exception(original);
            } catch (const Error& value) {
                error = value;
            } catch (...) {
            }
            {
                std::lock_guard<std::mutex> lock(lease->error_mutex);
                if (lease->terminal_error)
                    error = *lease->terminal_error;
            }
            handle_failure(
                lease,
                error
            );
            {
                std::lock_guard<std::mutex> lock(admission_mutex_);
                if (admitted_ == lease)
                    admitted_.reset();
                executing_.reset();
            }
            throw error;
        }
    });
    auto result = task->get_future();
    {
        std::lock_guard<std::mutex> admission(admission_mutex_);
        require(
            !termination_,
            XMAX_INVALID_STATE,
            "Realtime resource cleanup is in progress"
        );
        require(
            !admitted_ || (kind == OperationKind::Generation && admitted_->kind == OperationKind::Generation),
            XMAX_INVALID_STATE,
            "Another realtime operation is in progress; wait for it to finish"
        );
        std::lock_guard<std::mutex> work(work_mutex_);
        require(
            !exiting_,
            XMAX_INVALID_STATE,
            "Realtime lifecycle has shut down"
        );
        if (admitted_) {
            admitted_->replaced = true;
            admitted_->valid = false;
        }
        admitted_ = lease;
        work_.push_back([task] { (*task)(); });
    }
    wake_.notify_one();
    result.get();
}

/**
 * Queues one low-priority quality update, skipped while foreground work or termination is pending.
 *
 * @param[in] body The quality work, which must honor the supplied cancellation predicate.
 */
void XmaxRealtimeLifecycleManager::enqueue_adaptive(
    std::function<void(const Token&)> body
) {
    std::lock_guard<std::mutex> admission(admission_mutex_);
    if (adaptive_pending_ || admitted_ || executing_ || termination_ || state() != XMAX_GENERATING)
        return;
    std::lock_guard<std::mutex> work(work_mutex_);
    if (exiting_)
        return;
    adaptive_pending_ = true;
    background_work_.push_back([this, body = std::move(body)] {
        auto lease = std::make_shared<Lease>(OperationKind::Configuration);
        {
            std::lock_guard<std::mutex> lock(admission_mutex_);
            adaptive_pending_ = false;
            if (admitted_ || termination_ || state() != XMAX_GENERATING)
                return;
            executing_ = lease;
        }
        try {
            body(Token(lease));
        } catch (const Error& error) {
            if (error.code != XMAX_CANCELLED && errors_) {
                xmax_event event{};
                event.type = XMAX_EVENT_ERROR;
                event.state = state();
                event.code = error.code;
                copy_utf8(
                    event.message,
                    sizeof(event.message),
                    XmaxLogger::redact(error.what())
                );
                emit(
                    event,
                    XMAX_REASON_FAILURE,
                    &error
                );
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(admission_mutex_);
            executing_.reset();
            throw;
        }
        std::lock_guard<std::mutex> lock(admission_mutex_);
        executing_.reset();
    });
    wake_.notify_one();
}

/**
 * Determines whether scoped termination invalidates an operation kind.
 *
 * @param[in] scope The requested cleanup scope.
 * @param[in] kind The operation's resource kind.
 *
 * @return Whether termination releases resources used by the operation.
 */
bool XmaxRealtimeLifecycleManager::affects(
    TerminationScope scope,
    OperationKind kind
) {
    if (scope == TerminationScope::All)
        return true;
    if (scope == TerminationScope::Connection)
        return kind != OperationKind::Media;
    return kind == OperationKind::Generation || kind == OperationKind::Configuration || kind == OperationKind::CameraSwitch;
}

/**
 * Creates or broadens a termination request while cancelling affected operations.
 *
 * @param[in] scope The requested resource scope.
 * @param[in] reason The final state reason.
 * @param[in] error Optional original failure.
 * @param[in] origin The failed operation, or an empty reference for explicit termination.
 * @param[in] schedule Whether to enqueue cleanup instead of executing it on the current worker.
 * @param[in] current Optional producer validation checked atomically with termination admission.
 */
std::shared_ptr<XmaxRealtimeLifecycleManager::Termination> XmaxRealtimeLifecycleManager::request_termination(
    TerminationScope scope,
    xmax_realtime_reason reason,
    std::optional<Error> error,
    std::shared_ptr<Lease> origin,
    bool schedule,
    Cancel current
) {
    std::shared_ptr<Termination> pending;
    bool created = false;
    {
        std::lock_guard<std::mutex> lock(admission_mutex_);
        if (current && !current())
            return {};
        if (error && !origin && termination_ && termination_->scope != TerminationScope::Generation && scope != TerminationScope::All)
            return {};
        if (termination_) {
            pending = termination_;
            if (scope > pending->scope)
                pending->scope = scope;
        } else {
            pending = std::make_shared<Termination>(
                scope,
                reason
            );
            termination_ = pending;
            created = true;
        }
        if (error && !pending->error) {
            pending->error = std::move(error);
            pending->reason = XMAX_REASON_FAILURE;
        }
        const auto invalidate_operation = [&](const std::shared_ptr<Lease>& lease) {
            std::lock_guard<std::mutex> failure(lease->error_mutex);
            if (!lease->terminal_error)
                lease->terminal_error = pending->error;
            lease->valid = false;
        };
        if (!origin && admitted_ && affects(pending->scope, admitted_->kind))
            invalidate_operation(admitted_);
        if (executing_ && affects(
                              pending->scope,
                              executing_->kind
                          ))
            invalidate_operation(executing_);
        if (pending->scope != TerminationScope::Generation)
            invalidate();
    }
    if (created && schedule) {
        std::lock_guard<std::mutex> lock(work_mutex_);
        require(
            !exiting_,
            XMAX_INVALID_STATE,
            "Realtime lifecycle has shut down"
        );
        work_.push_front([this, pending] { perform_termination(pending); });
        wake_.notify_one();
    }
    return pending;
}

/**
 * Merges concurrent termination requests and waits for the broadest requested cleanup.
 *
 * @param[in] scope The resources to release.
 * @param[in] reason The reason delivered with the final connection state.
 *
 * @return Whether server-side resource release succeeded.
 */
bool XmaxRealtimeLifecycleManager::terminate(
    TerminationScope scope,
    xmax_realtime_reason reason
) {
    require(
        worker_.get_id() != std::this_thread::get_id(),
        XMAX_INVALID_STATE,
        "Reentrant lifecycle termination"
    );
    return request_termination(
               scope,
               reason
    )
        ->result.get();
}

/**
 * Schedules a scoped runtime failure while rejecting notifications from released resources.
 *
 * @param[in] error The original runtime failure.
 * @param[in] scope The affected resources.
 * @param[in] current The predicate verifying that the producer remains current.
 */
void XmaxRealtimeLifecycleManager::terminate_failure(
    Error error,
    TerminationScope scope,
    Cancel current
) {
    request_termination(
        scope,
        XMAX_REASON_FAILURE,
        std::move(error),
        {},
        true,
        std::move(current)
    );
}

/**
 * Applies an operation's rollback policy while preserving explicit termination ownership.
 *
 * @param[in] lease The failed or cancelled operation.
 * @param[in] error The execution error.
 */
void XmaxRealtimeLifecycleManager::handle_failure(
    const std::shared_ptr<Lease>& lease,
    const Error& error
) {
    {
        std::lock_guard<std::mutex> lock(admission_mutex_);
        if (termination_ || !lease->failure_scope || lease->effective_kind == OperationKind::Configuration)
            return;
    }
    auto scope = *lease->failure_scope;
    if (lease->replaced && (state() == XMAX_CONNECTED || state() == XMAX_GENERATING))
        scope = TerminationScope::Generation;
    auto pending = request_termination(
        scope,
        error.code == XMAX_CANCELLED ? XMAX_REASON_NORMAL : XMAX_REASON_FAILURE,
        error.code == XMAX_CANCELLED ? std::optional<Error>{} : std::optional<Error>{error},
        lease,
        false
    );
    perform_termination(pending);
}

/**
 * Releases merged resource scopes and completes all waiting termination callers.
 *
 * @param[in] pending The current shared termination request.
 */
void XmaxRealtimeLifecycleManager::perform_termination(
    const std::shared_ptr<Termination>& pending
) {
    bool success = true;
    for (;;) {
        TerminationScope scope;
        xmax_realtime_reason reason;
        std::optional<Error> error;
        {
            std::lock_guard<std::mutex> lock(admission_mutex_);
            if (termination_ != pending)
                return;
            scope = pending->scope;
            reason = pending->reason;
            error = pending->error;
        }
        if (errors_) {
            if (error)
                errors_->record(*error);
            else
                errors_->reset(reason);
        }
        auto previous = snapshot();
        if (scope != TerminationScope::Generation &&
            (state() == XMAX_CONNECTING || state() == XMAX_CONNECTED || state() == XMAX_GENERATING))
            publish({XMAX_DISCONNECTING, previous.event.session_id, {}});

        CleanupResult result;
        try {
            if (cleanup_)
                result = cleanup_(scope);
        } catch (const Error& value) {
            result.success = false;
            if (errors_ && !error)
                errors_->record(value);
        } catch (...) {
            result.success = false;
            if (errors_ && !error)
                errors_->record(Error(
                    XMAX_INTERNAL_ERROR,
                    "Realtime resource cleanup failed"
                ));
        }
        success = success && result.success;
        {
            std::lock_guard<std::mutex> lock(admission_mutex_);
            if (!result.success && !pending->error && errors_ && errors_->error()) {
                pending->error = *errors_->error();
                pending->reason = XMAX_REASON_FAILURE;
            }
            if (!result.success && scope == TerminationScope::Generation) {
                pending->scope = std::max(pending->scope, TerminationScope::Connection);
                invalidate();
            }
            if (scope != pending->scope)
                continue;
            error = pending->error;
            if (error && errors_)
                errors_->record(*error);
            if (admitted_ && !admitted_->valid && affects(scope, admitted_->kind))
                admitted_.reset();
            // Keep admission closed until the final state and failure revision have been committed.
            if (errors_)
                errors_->invalidate_pending_failures(scope);
            const auto final_state = scope == TerminationScope::Generation
                                         ? (previous.event.state == XMAX_CONNECTED || previous.event.state == XMAX_GENERATING ? XMAX_CONNECTED : previous.event.state)
                                         : (scope == TerminationScope::All || !result.has_local_media ? XMAX_IDLE : XMAX_READY);
            publish({final_state, result.session_id.empty() ? previous.event.session_id : result.session_id, {}});
            termination_.reset();
        }
        if (error) {
            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.state = state();
            event.code = error->code;
            const auto final_state = snapshot();
            copy_utf8(
                event.session_id,
                sizeof(event.session_id),
                final_state.event.session_id
            );
            copy_utf8(
                event.message,
                sizeof(event.message),
                XmaxLogger::redact(error->what())
            );
            emit(
                event,
                XMAX_REASON_FAILURE,
                &*error
            );
        }
        pending->completion.set_value(success);
        return;
    }
}

/**
 * Commits state from current work and queues the corresponding application event.
 *
 * @param[in] value The new state and business identifiers.
 */
void XmaxRealtimeLifecycleManager::commit(
    RealtimeState value
) {
    std::lock_guard<std::mutex> lock(admission_mutex_);
    if (executing_)
        Token(executing_).ensure_current();
    publish(std::move(value));
}

/**
 * Stages final state for publication after this operation releases foreground admission.
 *
 * @param[in] value The phase and identifiers whose resources are already committed.
 * @param[in] token The current operation's validity token.
 * @param[in] completed Optional bookkeeping performed after state publication on the worker.
 */
void XmaxRealtimeLifecycleManager::complete(
    RealtimeState value,
    const Token& token,
    std::function<void()> completed
) {
    std::lock_guard<std::mutex> lock(admission_mutex_);
    token.ensure_current();
    require(
        executing_ == token.lease_,
        XMAX_CANCELLED,
        "The operation no longer owns lifecycle completion"
    );
    token.lease_->completion_state = std::move(value);
    token.lease_->completion_handler = std::move(completed);
}

/**
 * Publishes state after current work or coordinated cleanup has committed its resources.
 *
 * @param[in] value The new phase and business identifiers.
 */
void XmaxRealtimeLifecycleManager::publish(
    RealtimeState value
) {
    xmax_event_info info{};
    info.struct_size = sizeof(info);
    info.event.struct_size = sizeof(info.event);
    info.event.type = XMAX_EVENT_STATE;
    info.event.state = value.connection_state;
    info.reason = errors_ ? errors_->reason() : XMAX_REASON_NORMAL;
    info.error.struct_size = sizeof(info.error);
    if (errors_) {
        info.error = errors_->details();
        info.event.code = info.error.code;
    }
    copy_utf8(
        info.event.session_id,
        sizeof(info.event.session_id),
        value.session_id
    );
    copy_utf8(
        info.event.task_id,
        sizeof(info.event.task_id),
        value.task_id
    );
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (snapshot_.event.state == info.event.state && snapshot_.reason == info.reason &&
            snapshot_.error.code == info.error.code && snapshot_.error.api_code == info.error.api_code &&
            snapshot_.error.http_status == info.error.http_status &&
            std::strcmp(
                snapshot_.error.message,
                info.error.message
            ) == 0 &&
            std::strcmp(
                snapshot_.event.session_id,
                info.event.session_id
            ) == 0 &&
            std::strcmp(
                snapshot_.event.task_id,
                info.event.task_id
            ) == 0)
            return;
        snapshot_ = info;
        state_ = value.connection_state;
    }
    if (state_handler_)
        state_handler_(value);
    emit(
        info.event,
        info.reason,
        errors_ ? errors_->error() : nullptr
    );
    XmaxLogger::write(
        LogCategory::Realtime,
        XMAX_LOG_INFO,
        [&](auto language) {
            static const char* names[]{"idle", "ready", "connecting", "connected", "generating", "disconnecting", "preparing"};
            return XmaxLogger::localized(
                       language,
                       "状态更新 (State Changed)",
                       "State Changed"
                   ) +
                   "\n├─ " + names[value.connection_state] + "\n├─ session=" + value.session_id + "\n└─ task=" + value.task_id;
        }
    );
}

/**
 * Returns a state event without waiting for blocking resource work.
 */
xmax_event_info XmaxRealtimeLifecycleManager::snapshot() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return snapshot_;
}

/**
 * Stops accepting work, drains the queue, and waits for worker completion.
 */
void XmaxRealtimeLifecycleManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        exiting_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

/**
 * Queues asynchronous lifecycle bookkeeping; shutdown ignores later submissions.
 *
 * @param[in] fn The worker operation.
 */
void XmaxRealtimeLifecycleManager::enqueue(
    std::function<void()> fn
) {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        if (exiting_)
            return;
        work_.push_back(std::move(fn));
    }
    wake_.notify_one();
}

/**
 * Runs bookkeeping on the lifecycle worker and propagates its result or exception.
 *
 * @param[in] fn The work to serialize without starting a resource operation.
 */
void XmaxRealtimeLifecycleManager::sync(
    std::function<void()> fn
) {
    if (worker_.get_id() == std::this_thread::get_id()) {
        fn();
        return;
    }
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
    auto result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        require(
            !exiting_,
            XMAX_INVALID_STATE,
            "Realtime lifecycle has shut down"
        );
        work_.push_back([task] { (*task)(); });
    }
    wake_.notify_one();
    result.get();
}

/**
 * Executes foreground work, low-priority adaptation, and due heartbeats.
 */
void XmaxRealtimeLifecycleManager::worker_loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(work_mutex_);
            wake_.wait_until(
                lock,
                deadline_,
                [this] { return exiting_ || !work_.empty() || !background_work_.empty(); }
            );
            if (exiting_ && work_.empty() && background_work_.empty())
                return;
            auto& queue = work_.empty() ? background_work_ : work_;
            if (!queue.empty()) {
                job = std::move(queue.front());
                queue.pop_front();
            }
        }
        try {
            if (job)
                job();
            if (Clock::now() >= deadline_ && tick_)
                tick_();
        } catch (...) {
            deadline_ = Clock::time_point::max();
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
 * Returns a cancellation predicate scoped to an RTC connection revision.
 *
 * @param[in] epoch The connection revision captured by the producer.
 */
Cancel XmaxRealtimeLifecycleManager::cancellation(
    uint64_t epoch
) {
    return [this, epoch] { return epoch_.load() != epoch; };
}

/**
 * Queues a bounded application event with its reason and copied error details.
 *
 * @param[in] event The event to deliver.
 * @param[in] reason The state-transition reason.
 * @param[in] error Optional associated failure information.
 */
void XmaxRealtimeLifecycleManager::emit(
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
 * Removes the oldest application event without blocking.
 *
 * @param[out] event Receives the event when one is available.
 *
 * @return Whether an event was retrieved.
 */
bool XmaxRealtimeLifecycleManager::poll(
    xmax_event& event
) {
    xmax_event_info info{};
    if (!poll(info))
        return false;
    event = info.event;
    return true;
}

/**
 * Removes the oldest application event with its reason and original error details.
 *
 * @param[out] info Receives the event when one is available.
 *
 * @return Whether an event was retrieved.
 */
bool XmaxRealtimeLifecycleManager::poll(
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
