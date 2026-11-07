//
//  RemoteVideoFramePipeline.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Video/RemoteVideoFramePipeline.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {

/**
 * Enables frame delivery for a revision and discards previously pending frames.
 *
 * @param[in] token The connection or media-binding revision used to reject stale callbacks.
 */
void RemoteVideoFramePipeline::activate(
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (token < token_)
        return;

    token_ = token;
    latest_.reset();
    received_ = false;
    active_ = true;
    changed_.notify_all();
    XmaxLogger::write(
        LogCategory::Render,
        XMAX_LOG_DEBUG,
        [](auto language) {
            return XmaxLogger::localized(
                language,
                "预览绑定已激活 (Preview Binding Activated)",
                "Preview Binding Activated"
            );
        }
    );
}

/**
 * Disables frame delivery and clears pending frames.
 *
 * @param[in] token The connection or media-binding revision used to reject stale callbacks.
 */
void RemoteVideoFramePipeline::deactivate(
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_)
        return;
    token_ = token;
    active_ = false;
    latest_.reset();
    changed_.notify_all();
}

/**
 * Accepts frames only for the current revision, replacing the pending frame.
 *
 * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
 * synchronous call returns.
 * @param[in] token The connection or media-binding revision used to reject stale callbacks.
 */
void RemoteVideoFramePipeline::submit(
    Frame frame,
    uint64_t token
) {
    submit_owned(
        std::make_shared<Frame>(std::move(frame)),
        token
    );
}

/**
 * Shares already-owned pixels with the active mailbox without copying their buffers.
 *
 * @param[in] frame The owned read-only frame to share.
 * @param[in] token The producer's binding revision.
 *
 * @return Whether the active binding accepted this frame.
 */
bool RemoteVideoFramePipeline::submit_owned(
    std::shared_ptr<Frame> frame,
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || token != token_)
        return false;
    latest_ = std::move(frame);
    if (!std::exchange(received_, true))
        changed_.notify_all();
    return true;
}

void RemoteVideoFramePipeline::wait_ready(uint64_t token, Clock::time_point deadline, const Cancel& cancel) {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        require(active_ && token_ == token && !cancel(), XMAX_CANCELLED, "Result video binding was cleared");
        if (received_)
            return;
        require(Clock::now() < deadline, XMAX_TIMEOUT, "Remote video first frame timed out");
        changed_.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(20)));
    }
}

/**
 * Acquires the latest frame without blocking and clears the mailbox.
 *
 * @return An independently owned latest frame, or nullptr if none is pending.
 */
std::shared_ptr<Frame> RemoteVideoFramePipeline::acquire(uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (revision && revision - 1 != token_)
        return {};
    return std::exchange(
        latest_,
        {}
    );
}
} // namespace xmax::detail
