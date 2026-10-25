//
//  RenderController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Render/Video/RemoteVideoFramePipeline.hpp"
#include "Render/Video/RemoteMediaFrameDispatcher.hpp"
#include "Render/Trajectory/TrajectoryRegistry.hpp"
#include "Render/Trajectory/DefaultTrajectoryEffectRenderer.hpp"

namespace xmax::detail {

/**
 * Retains the latest local and remote frames while rejecting stale session or media callbacks.
 */
class RenderController {

  public:

    /**
     * The task trajectory cache shared by default effects and custom rendering.
     */
    TrajectoryRegistry trajectory;

    /**
     * Enables remote result delivery for a connection revision.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void bind(
        uint64_t token
    ) {
        remote_.activate(token);
        output_.bind(token);
    }

    /**
     * Disables remote result delivery and clears pending frames.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void unbind(
        uint64_t token
    ) {
        remote_.deactivate(token);
        output_.unbind(token);
    }

    /**
     * Receives remote frames only for the current connection revision.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void receive(
        Frame frame,
        uint64_t token
    ) {
        auto owned = std::make_shared<Frame>(std::move(frame));
        if (remote_.submit_owned(
            owned,
            token
        ))
            output_.video(
                std::move(owned),
                token
            );
    }

    /**
     * Receives copied PCM for the current result binding without changing native playback.
     *
     * @param[in] frame The result user's owned PCM and native rendering time.
     * @param[in] token The producer's connection revision.
     */
    void receive_audio(
        AudioFrame frame,
        uint64_t token
    ) {
        output_.audio(
            std::move(frame),
            token
        );
    }

    /**
     * Replaces application media delivery and discards packets queued for the previous callback.
     *
     * @param[in] listener The owned-packet callback, or an empty function to stop delivery.
     *
     * @return The previous callback, whose context must be released outside the lifecycle queue.
     */
    RemoteMediaListener media_listener(
        RemoteMediaListener listener
    ) {
        return output_.listener(std::move(listener));
    }

    /**
     * Reports whether application media delivery currently requires native PCM observation.
     */
    bool has_media_listener() const {
        return output_.has_listener();
    }

    /**
     * Drains application media delivery while the lifecycle executor still accepts callback reentry.
     */
    void shutdown_output() {
        output_.shutdown();
    }

    /**
     * Acquires the latest remote result frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     */
    std::shared_ptr<Frame> acquire_frame() {
        auto frame = remote_.acquire();
        if (frame)
            frame->presentation_mirrored = remote_mirrored_.load();
        return frame;
    }

    /**
     * Enables local preview delivery for a media revision.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void bind_local(
        uint64_t token
    ) {
        local_.activate(token);
    }

    /**
     * Disables local preview delivery and clears pending frames.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void unbind_local(
        uint64_t token
    ) {
        local_.deactivate(token);
    }

    /**
     * Receives local preview frames only for the current media revision.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void receive_local(
        Frame frame,
        uint64_t token
    ) {
        local_.submit(
            std::move(frame),
            token
        );
    }

    /**
     * Acquires the latest local preview frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     */
    std::shared_ptr<Frame> acquire_local_frame() {
        auto frame = local_.acquire();
        if (frame)
            frame->presentation_mirrored = local_mirrored_.load();
        return frame;
    }

    /**
     * Updates display orientation for subsequently acquired frames without modifying pixels.
     *
     * @param[in] local Whether to horizontally mirror local preview.
     * @param[in] remote Whether to horizontally mirror the remote result display.
     */
    void mirror(
        bool local,
        bool remote
    ) {
        local_mirrored_ = local;
        remote_mirrored_ = remote;
    }

    /**
     * Renders a result frame and the task's default trajectory in a native window.
     *
     * @param[in] frame The currently displayed result frame.
     * @param[in] window An HWND owned by the calling thread.
     * @param[in] content_mode The layout mode shared by video and trajectory rendering.
     * @param[in] interaction_enabled Whether the current generation permits interaction effects.
     */
    void render_window(
        const Frame& frame,
        void* window,
        int32_t content_mode,
        bool interaction_enabled
    ) const {
        xmax::detail::render_window(
            frame,
            window,
            content_mode
        );
        if (interaction_enabled)
            trajectory_renderer_
                .render(
                window,
                {frame.width, frame.height, 30},
                content_mode,
                trajectory.snapshot()
            );
    }

  private:

    /**
     * The display-mirror setting captured when acquiring a local frame.
     */
    std::atomic<bool> local_mirrored_{false};

    /**
     * The display-mirror setting captured when acquiring a remote frame.
     */
    std::atomic<bool> remote_mirrored_{false};

    /**
     * The native window renderer for default trajectory effects.
     */
    DefaultTrajectoryEffectRenderer trajectory_renderer_;

    /**
     * The latest remote result-frame mailbox.
     */
    RemoteVideoFramePipeline remote_;

    /**
     * The latest local preview-frame mailbox.
     */
    RemoteVideoFramePipeline local_;

    /**
     * The bounded serial audio/video output independent of latest-frame presentation mailboxes.
     */
    RemoteMediaFrameDispatcher output_;
};
} // namespace xmax::detail
