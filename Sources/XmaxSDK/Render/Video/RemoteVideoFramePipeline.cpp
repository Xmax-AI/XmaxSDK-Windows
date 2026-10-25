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
    active_ = true;
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
    return true;
}

/**
 * Acquires the latest frame without blocking and clears the mailbox.
 *
 * @return An independently owned latest frame, or nullptr if none is pending.
 */
std::shared_ptr<Frame> RemoteVideoFramePipeline::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(
        latest_,
        {}
    );
}
} // namespace xmax::detail
