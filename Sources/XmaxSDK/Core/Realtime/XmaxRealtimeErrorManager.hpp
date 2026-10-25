//
//  XmaxRealtimeErrorManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
#include <mutex>

namespace xmax::detail {

/**
 * The resources released by a realtime termination request, ordered from narrowest to broadest.
 */
enum class RealtimeTerminationScope {

    /**
     * Releases the generation task while retaining the RTC connection and local media.
     */
    Generation,

    /**
     * Releases generation, RTC, and the server session while retaining local preview.
     */
    Connection,

    /**
     * Releases connection resources and local media bindings.
     */
    All
};

/**
 * Retains the state reason and original error and builds redacted error snapshots. Access is confined to the
 * lifecycle thread. Runtime failures retain media or connection revisions until coordinated termination.
 */
class XmaxRealtimeErrorManager {

  public:

    /**
     * Routes a current runtime failure to lifecycle termination without blocking its producer.
     *
     * The validity predicate must be checked before cleanup; it rejects failures from released resources.
     */
    using FailureHandler = std::function<void(Error, RealtimeTerminationScope, Cancel)>;

    /**
     * Installs the runtime failure receiver before starting media or RTC resources.
     *
     * @param[in] handler The receiver for a copied error, cleanup scope, and current-resource predicate.
     */
    void failure_handler(
        FailureHandler handler
    ) {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        failure_handler_ = std::move(handler);
    }

    /**
     * Forwards a failure only while its resource revision and optional producer predicate remain current.
     *
     * @param[in] error The original runtime failure.
     * @param[in] scope The resources affected by this failure.
     * @param[in] current Optional producer-specific resource validation.
     */
    void forward(
        Error error,
        RealtimeTerminationScope scope,
        Cancel current = {}
    ) {
        FailureHandler handler;
        uint64_t revision = 0;
        {
            std::lock_guard<std::mutex> lock(failure_mutex_);
            handler = failure_handler_;
            revision = scope == RealtimeTerminationScope::All ? media_revision_ : connection_revision_;
        }
        if (handler)
            handler(
                std::move(error),
                scope,
                [this, revision, scope, current = std::move(current)] {
                    std::lock_guard<std::mutex> lock(failure_mutex_);
                    return revision == (scope == RealtimeTerminationScope::All ? media_revision_ : connection_revision_) &&
                           (!current || current());
                }
            );
    }

    /**
     * Invalidates failures produced by resources released during cleanup.
     *
     * @param[in] scope The resources that have finished termination.
     */
    void invalidate_pending_failures(
        RealtimeTerminationScope scope
    ) {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        if (scope != RealtimeTerminationScope::Generation)
            ++connection_revision_;
        if (scope == RealtimeTerminationScope::All)
            ++media_revision_;
    }

    /**
     * Begins a normal operation and clears the previous failure reason.
     *
     * @param[in] reason A state-transition reason, such as normal stop or display-orientation change.
     */
    void reset(
        xmax_realtime_reason reason = XMAX_REASON_NORMAL
    ) {
        reason_ = reason;
        error_.reset();
    }

    /**
     * Retains the original failure, including service and HTTP status codes.
     *
     * @param[in] error The current operation's original SDK error.
     */
    void record(
        Error error
    ) {
        reason_ = XMAX_REASON_FAILURE;
        error_ = std::move(error);
    }

    /**
     * Returns the state-transition reason for the current state.
     */
    xmax_realtime_reason reason() const {
        return reason_;
    }

    /**
     * Returns a borrowed error pointer, or null without a failure; invalidated by the next mutation.
     */
    const Error* error() const {
        return error_ ? &*error_ : nullptr;
    }

    /**
     * Builds an owned, redacted error snapshot for the C ABI.
     */
    xmax_error_info details() const {
        xmax_error_info info{};
        info.struct_size = sizeof(info);
        if (!error_)
            return info;

        info.code = error_->code;
        info.api_code = error_->api_code;
        info.http_status = error_->http_status;

        const auto message = XmaxLogger::redact(error_->what());
        auto length = std::min(message.size(), sizeof(info.message) - 1);
        while (length && length < message.size() &&
               (static_cast<unsigned char>(message[length]) & 0xc0) == 0x80)
            --length;
        std::memcpy(
            info.message,
            message.data(),
            length
        );
        return info;
    }

  private:

    /**
     * Protects runtime failure routing and resource revisions across producer threads.
     */
    mutable std::mutex failure_mutex_;

    /**
     * The lifecycle receiver installed by component assembly.
     */
    FailureHandler failure_handler_;

    /**
     * The validity revision for local capture and external input failures.
     */
    uint64_t media_revision_ = 0;

    /**
     * The validity revision for RTC and session failures.
     */
    uint64_t connection_revision_ = 0;

    /**
     * The state-transition reason for the latest valid operation.
     */
    xmax_realtime_reason reason_ = XMAX_REASON_NORMAL;

    /**
     * The original error associated with the current state.
     */
    std::optional<Error> error_;
};
} // namespace xmax::detail
