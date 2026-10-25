//
//  XmaxRealtimeLifecycleManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/XmaxRealtimeErrorManager.hpp"
#include "Service/Realtime/RealtimeState.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

namespace xmax::detail {

/**
 * Owns operation admission, state, cancellation, and scoped termination for one realtime instance.
 * Resource controllers execute on its serial worker; injected cleanup releases their resources.
 */
class XmaxRealtimeLifecycleManager {

  public:

    /**
     * The resource scopes supported by lifecycle termination.
     */
    using TerminationScope = RealtimeTerminationScope;

    /**
     * The realtime operations that share resource ownership.
     */
    enum class OperationKind {

        /**
         * Creates, stops, or reconfigures local media.
         */
        Media,

        /**
         * Establishes a server session and RTC connection.
         */
        Connection,

        /**
         * Starts generation or replaces a pending generation request.
         */
        Generation,

        /**
         * Switches capture devices and resumes generation when required.
         */
        CameraSwitch,

        /**
         * Changes conditions or transport settings while retaining the lifecycle.
         */
        Configuration
    };

  private:

    /**
     * The shared validity and rollback controls for an admitted operation.
     */
    struct Lease {

        /**
         * Whether this operation can still publish successful results.
         */
        std::atomic<bool> valid{true};

        /**
         * Whether a later generation request replaced this operation.
         */
        std::atomic<bool> replaced{false};

        /**
         * Protects the first terminal runtime failure across cancellation producers and the worker.
         */
        mutable std::mutex error_mutex;

        /**
         * The original runtime failure returned to work cancelled by scoped failure termination.
         */
        std::optional<Error> terminal_error;

        /**
         * The admission kind, retained when generation becomes a conditions update.
         */
        OperationKind kind;

        /**
         * The effective operation kind resolved when execution begins.
         */
        OperationKind effective_kind;

        /**
         * The resources to release on execution failure, set only after validation.
         */
        std::optional<TerminationScope> failure_scope;

        /**
         * The final state held until this operation releases foreground admission.
         */
        std::optional<RealtimeState> completion_state;

        /**
         * The worker callback performed after successful state commitment.
         */
        std::function<void()> completion_handler;

        /**
         * Creates the operation's immutable admission identity.
         */
        explicit Lease(
            OperationKind value
        ) : kind(value), effective_kind(value) {
        }
    };

  public:

    /**
     * Guards an operation across blocking resource calls and state commitments.
     */
    class Token {

      public:

        /**
         * Rejects results from an operation that was terminated or replaced.
         */
        void ensure_current() const;

        /**
         * Returns whether this operation can still commit results.
         */
        bool is_current() const;

        /**
         * Sets the resources released if this operation fails after validation.
         *
         * @param[in] scope The rollback scope for this operation.
         */
        void failure_scope(
            TerminationScope scope
        ) const;

        /**
         * Returns a cancellation predicate that retains this operation's validity record.
         */
        Cancel cancellation() const;

      private:

        friend class XmaxRealtimeLifecycleManager;

        /**
         * Retains the validity record independently of queued or executing work.
         */
        std::shared_ptr<Lease> lease_;

        /**
         * Creates a token for an admitted operation.
         */
        explicit Token(
            std::shared_ptr<Lease> lease
        ) : lease_(std::move(lease)) {
        }
    };

    /**
     * The remaining resources and identifiers after scoped cleanup.
     */
    struct CleanupResult {

        /**
         * The current or most recently released server session identifier.
         */
        std::string session_id;

        /**
         * Whether local media remains available for preview and reconnection.
         */
        bool has_local_media = false;

        /**
         * Whether server-side resource release completed successfully.
         */
        bool success = true;
    };

    /**
     * Releases resources on the lifecycle worker and reports what remains available.
     */
    using CleanupHandler = std::function<CleanupResult(TerminationScope)>;

    /**
     * Receives committed state on the lifecycle worker before its application event is queued.
     */
    using StateHandler = std::function<void(const RealtimeState&)>;

    /**
     * Starts the serial worker with heartbeat and asynchronous failure receivers.
     *
     * @param[in] tick The due-heartbeat handler.
     * @param[in] failure The handler for unexpected asynchronous exceptions.
     */
    explicit XmaxRealtimeLifecycleManager(
        std::function<void()> tick,
        std::function<void(Error)> failure = {}
    );

    /**
     * Drains accepted work and stops the lifecycle worker.
     */
    ~XmaxRealtimeLifecycleManager();

    /**
     * Installs resource cleanup, shared errors, and state bookkeeping before admitting operations.
     *
     * @param[in] errors The error manager, which outlives this lifecycle manager.
     * @param[in] cleanup The resource-release handler; it must not submit blocking lifecycle work.
     * @param[in] state_handler Optional state bookkeeping performed before event delivery.
     */
    void configure(
        XmaxRealtimeErrorManager& errors,
        CleanupHandler cleanup,
        StateHandler state_handler = {}
    );

    /**
     * Admits a foreground operation; generation replaces earlier generation work after its cleanup.
     *
     * @param[in] kind The resource operation being requested.
     * @param[in] body The blocking work, which checks its token across resource calls.
     *
     * @note Conflicting foreground operations fail with XMAX_INVALID_STATE. Do not call from the worker.
     */
    void run(
        OperationKind kind,
        const std::function<void(const Token&)>& body
    );

    /**
     * Queues one low-priority quality update, skipped while foreground work or termination is pending.
     *
     * @param[in] body The quality work, which must honor the supplied cancellation predicate.
     */
    void enqueue_adaptive(
        std::function<void(const Token&)> body
    );

    /**
     * Merges concurrent termination requests and waits for the broadest requested cleanup.
     *
     * @param[in] scope The resources to release.
     * @param[in] reason The reason delivered with the final connection state.
     *
     * @return Whether server-side resource release succeeded.
     */
    bool terminate(
        TerminationScope scope,
        xmax_realtime_reason reason = XMAX_REASON_NORMAL
    );

    /**
     * Schedules a scoped runtime failure while rejecting notifications from released resources.
     *
     * @param[in] error The original runtime failure.
     * @param[in] scope The affected resources.
     * @param[in] current The predicate verifying that the producer remains current.
     */
    void terminate_failure(
        Error error,
        TerminationScope scope,
        Cancel current
    );

    /**
     * Commits state from current work and queues the corresponding application event.
     *
     * @param[in] value The new state and business identifiers.
     */
    void commit(
        RealtimeState value
    );

    /**
     * Stages final state for publication after this operation releases foreground admission.
     *
     * @param[in] value The phase and identifiers whose resources are already committed.
     * @param[in] token The current operation's validity token.
     * @param[in] completed Optional bookkeeping performed after state publication on the worker.
     */
    void complete(
        RealtimeState value,
        const Token& token,
        std::function<void()> completed = {}
    );

    /**
     * Returns a state event without waiting for blocking resource work.
     */
    xmax_event_info snapshot() const;

    /**
     * Returns the current connection phase atomically.
     */
    xmax_state state() const {
        return state_.load();
    }

    /**
     * Stops accepting work, drains the queue, and waits for worker completion.
     */
    void shutdown();

    /**
     * Runs bookkeeping on the lifecycle worker and propagates its result or exception.
     *
     * @param[in] fn The work to serialize without starting a resource operation.
     */
    void sync(
        std::function<void()> fn
    );

    /**
     * Queues asynchronous lifecycle bookkeeping; shutdown ignores later submissions.
     *
     * @param[in] fn The worker operation.
     */
    void enqueue(
        std::function<void()> fn
    );

    /**
     * Returns the RTC connection revision used by native callback producers.
     */
    uint64_t epoch() const {
        return epoch_.load();
    }

    /**
     * Invalidates callbacks and publication from the previous RTC connection.
     *
     * @return The new connection revision.
     */
    uint64_t invalidate() {
        return ++epoch_;
    }

    /**
     * Returns a cancellation predicate scoped to an RTC connection revision.
     *
     * @param[in] epoch The connection revision captured by the producer.
     */
    Cancel cancellation(
        uint64_t epoch
    );

    /**
     * Queues a bounded application event with its reason and copied error details.
     *
     * @param[in] event The event to deliver.
     * @param[in] reason The state-transition reason.
     * @param[in] error Optional associated failure information.
     */
    void emit(
        xmax_event event,
        xmax_realtime_reason reason = XMAX_REASON_NORMAL,
        const Error* error = nullptr
    );

    /**
     * Removes the oldest application event without blocking.
     *
     * @param[out] event Receives the event when one is available.
     *
     * @return Whether an event was retrieved.
     */
    bool poll(
        xmax_event& event
    );

    /**
     * Removes the oldest application event with its reason and original error details.
     *
     * @param[out] info Receives the event when one is available.
     *
     * @return Whether an event was retrieved.
     */
    bool poll(
        xmax_event_info& info
    );

    /**
     * Updates the worker-owned heartbeat deadline.
     *
     * @param[in] value The next deadline on the monotonic clock.
     */
    void deadline(
        Clock::time_point value
    ) {
        deadline_ = value;
    }

    /**
     * Returns the worker-owned heartbeat deadline.
     */
    Clock::time_point deadline() const {
        return deadline_;
    }

  private:

    /**
     * A merged termination request shared by all synchronous termination callers.
     */
    struct Termination {

        /**
         * The broadest resource scope requested so far.
         */
        TerminationScope scope;

        /**
         * The final connection-state reason, unless an original failure takes precedence.
         */
        xmax_realtime_reason reason;

        /**
         * The first failure responsible for this termination.
         */
        std::optional<Error> error;

        /**
         * Completes after resource release and final state publication.
         */
        std::promise<bool> completion;

        /**
         * Allows concurrent termination callers to await the same release operation.
         */
        std::shared_future<bool> result = completion.get_future().share();

        /**
         * Retains the initial cleanup scope and reason.
         */
        Termination(
            TerminationScope target,
            xmax_realtime_reason why
        ) : scope(target), reason(why) {
        }
    };

    /**
     * The due-heartbeat handler executed by the worker.
     */
    std::function<void()> tick_;

    /**
     * The fallback receiver for unexpected asynchronous failures.
     */
    std::function<void(Error)> failure_;

    /**
     * The shared error manager installed before resource operations begin.
     */
    XmaxRealtimeErrorManager* errors_ = nullptr;

    /**
     * The injected resource cleanup operation.
     */
    CleanupHandler cleanup_;

    /**
     * The resource bookkeeping callback invoked after a state commit.
     */
    StateHandler state_handler_;

    /**
     * The worker serializing resource use and cleanup.
     */
    std::thread worker_;

    /**
     * Protects accepted jobs and shutdown admission.
     */
    std::mutex work_mutex_;

    /**
     * Wakes the worker for jobs, termination, or shutdown.
     */
    std::condition_variable wake_;

    /**
     * Foreground operations and lifecycle bookkeeping in execution order.
     */
    std::deque<std::function<void()>> work_;

    /**
     * Low-priority jobs considered only after foreground work.
     */
    std::deque<std::function<void()>> background_work_;

    /**
     * Whether the worker has stopped accepting submissions.
     */
    bool exiting_ = false;

    /**
     * Protects operation admission and concurrent termination merging.
     */
    std::mutex admission_mutex_;

    /**
     * The latest foreground lease, including work waiting for predecessor cleanup.
     */
    std::shared_ptr<Lease> admitted_;

    /**
     * The lease currently executing on the worker.
     */
    std::shared_ptr<Lease> executing_;

    /**
     * The pending or executing merged termination request.
     */
    std::shared_ptr<Termination> termination_;

    /**
     * Whether a quality update is already waiting or executing.
     */
    bool adaptive_pending_ = false;

    /**
     * Protects application state snapshots independently of blocking resource work.
     */
    mutable std::mutex state_mutex_;

    /**
     * The latest state, identifiers, reason, and original error delivered to your app.
     */
    xmax_event_info snapshot_{};

    /**
     * The connection phase available to producer threads without locking.
     */
    std::atomic<xmax_state> state_{XMAX_IDLE};

    /**
     * The RTC connection revision, independent of replaceable generation operations.
     */
    std::atomic<uint64_t> epoch_{0};

    /**
     * Protects the bounded application event queue.
     */
    std::mutex event_mutex_;

    /**
     * The events waiting for application polling.
     */
    std::deque<xmax_event_info> events_;

    /**
     * The next heartbeat deadline, confined to the worker.
     */
    Clock::time_point deadline_ = Clock::time_point::max();

    /**
     * Executes foreground work, low-priority adaptation, and due heartbeats.
     */
    void worker_loop();

    /**
     * Determines whether scoped termination invalidates an operation kind.
     *
     * @param[in] scope The requested cleanup scope.
     * @param[in] kind The operation's resource kind.
     *
     * @return Whether termination releases resources used by the operation.
     */
    static bool affects(
        TerminationScope scope,
        OperationKind kind
    );

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
    std::shared_ptr<Termination> request_termination(
        TerminationScope scope,
        xmax_realtime_reason reason,
        std::optional<Error> error = {},
        std::shared_ptr<Lease> origin = {},
        bool schedule = true,
        Cancel current = {}
    );

    /**
     * Releases merged resource scopes and completes all waiting termination callers.
     *
     * @param[in] pending The current shared termination request.
     */
    void perform_termination(
        const std::shared_ptr<Termination>& pending
    );

    /**
     * Applies an operation's rollback policy while preserving explicit termination ownership.
     *
     * @param[in] lease The failed or cancelled operation.
     * @param[in] error The execution error.
     */
    void handle_failure(
        const std::shared_ptr<Lease>& lease,
        const Error& error
    );

    /**
     * Publishes state after current work or coordinated cleanup has committed its resources.
     *
     * @param[in] value The new phase and business identifiers.
     */
    void publish(
        RealtimeState value
    );
};
} // namespace xmax::detail
