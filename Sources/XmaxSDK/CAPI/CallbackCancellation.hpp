//
//  CallbackCancellation.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * Revokes a caller-owned cancellation callback before the C operation returns.
 *
 * Signaling attempts may retain a predicate after the caller releases its context.
 */
class CallbackCancellation {

    /**
     * Shares the borrowed callback and its revocation state with pending attempts.
     */
    struct State {

        /**
         * Serializes callback invocation and revocation.
         */
        std::mutex mutex;

        /**
         * The borrowed callback, or null after revocation.
         */
        xmax_cancellation_fn callback = nullptr;

        /**
         * The caller-owned context valid while the callback is active.
         */
        void* context = nullptr;
    };

    /**
     * The callback state retained by each predicate, or empty when no callback was supplied.
     */
    std::shared_ptr<State> state_;

  public:

    /**
     * Borrows a callback and its context for one blocking operation.
     *
     * @param[in] callback The optional cancellation callback.
     * @param[in] context The context borrowed until destruction.
     */
    CallbackCancellation(
        xmax_cancellation_fn callback,
        void* context
    ) {
        if (callback) {
            state_ = std::make_shared<State>();
            state_->callback = callback;
            state_->context = context;
        }
    }

    /**
     * Disallows copying the callback's revocation owner.
     *
     * @param[in] other The owner that cannot be copied.
     */
    CallbackCancellation(
        const CallbackCancellation& other
    ) = delete;

    /**
     * Disallows assigning the callback's revocation owner.
     *
     * @param[in] other The owner that cannot be copied.
     */
    CallbackCancellation& operator=(
        const CallbackCancellation& other
    ) = delete;

    /**
     * Waits for an active callback and prevents future access to the borrowed context.
     */
    ~CallbackCancellation() {
        if (!state_)
            return;

        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->callback = nullptr;
    }

    /**
     * Returns an optional predicate that remains safe after the borrowed callback is revoked.
     */
    Cancel predicate() const {
        if (!state_)
            return {};

        return [state = state_] {
            std::lock_guard<std::mutex> lock(state->mutex);
            return !state->callback || state->callback(state->context) != 0;
        };
    }
};

} // namespace xmax::detail
