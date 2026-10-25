//
//  RemoteVideoFramePipeline.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * Retains only the current revision's latest frame, transferring ownership on acquisition without
 * accumulating history.
 */
class RemoteVideoFramePipeline {

  public:

    /**
     * Enables frame delivery for a revision and discards previously pending frames.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void activate(
        uint64_t token
    );

    /**
     * Disables frame delivery and clears pending frames.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void deactivate(
        uint64_t token
    );

    /**
     * Accepts frames only for the current revision, replacing the pending frame.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void submit(
        Frame frame,
        uint64_t token
    );

    /**
     * Shares already-owned pixels with the active mailbox without copying their buffers.
     *
     * @param[in] frame The owned read-only frame to share.
     * @param[in] token The producer's binding revision.
     *
     * @return Whether the active binding accepted this frame.
     */
    bool submit_owned(
        std::shared_ptr<Frame> frame,
        uint64_t token
    );

    /**
     * Acquires the latest frame without blocking and clears the mailbox.
     *
     * @return An independently owned latest frame, or nullptr if none is pending.
     */
    std::shared_ptr<Frame> acquire();

  private:

    /**
     * The mutex protecting frame revision, reception state, and the pending frame.
     */
    std::mutex mutex_;

    /**
     * The revision of currently accepted frames.
     */
    uint64_t token_ = 0;

    /**
     * Whether frames for the current revision are accepted.
     */
    bool active_ = false;

    /**
     * The latest pending frame; new frames replace it and acquisition clears it.
     */
    std::shared_ptr<Frame> latest_;
};
} // namespace xmax::detail
