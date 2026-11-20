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
#include "Render/Beauty/RemoteVideoBeautyPipeline.hpp"
#include "Render/Video/RemoteMediaFrameDispatcher.hpp"
#include "Render/Trajectory/TrajectoryRegistry.hpp"
#include "Render/Trajectory/DefaultTrajectoryEffectRenderer.hpp"

namespace xmax::detail {

/**
 * Retains the latest local and remote frames while rejecting stale session or media callbacks.
 */
class RenderController {

  public:

    explicit RenderController(BeautyProcessorFactory factory = make_video_beauty_processor)
        : beauty_([this](std::shared_ptr<Frame> frame, uint64_t token) {
              if (remote_.submit_owned(frame, token))
                  output_.video(std::move(frame), token);
          }, std::move(factory)) {}

    void beauty(std::optional<BeautyOptions> options) { beauty_.configure(options); }
    BeautyErrorListener beauty_listener(BeautyErrorListener listener) { return beauty_.listener(std::move(listener)); }
    void close_beauty() { beauty_.reset(); }

    /** Waits for a processed result frame after the remote video publication was confirmed. */
    void wait_displayable_result(uint64_t token, const Cancel& cancel) {
        remote_.wait_ready(token, Clock::now() + std::chrono::seconds(10), cancel);
    }

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
        std::lock_guard<std::mutex> lock(result_binding_);
        result_token_ = token;
        result_bound_ = true;
        remote_.activate(token);
        output_.bind(token);
        beauty_.bind(token, true);
    }

    /**
     * Disables remote result delivery and clears pending frames.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     */
    void unbind(
        uint64_t token
    ) {
        std::lock_guard<std::mutex> lock(result_binding_);
        result_bound_ = false;
        beauty_.bind(token, false);
        remote_.deactivate(token);
        output_.unbind(token);
        ++result_revision_;
    }

    /** Discards the unpublished result and waits for a new generation binding. */
    void clear_result(uint64_t token) {
        std::lock_guard<std::mutex> lock(result_binding_);
        if (!result_bound_ || token != result_token_)
            return;
        result_bound_ = false;
        beauty_.bind(token, false);
        remote_.deactivate(token);
        output_.unbind(token);
        ++result_revision_;
    }

    /** Acquires a result frame and its content revision under the same binding lock. */
    std::shared_ptr<Frame> acquire_result(uint64_t track, uint64_t& content_revision) {
        std::lock_guard<std::mutex> lock(result_binding_);
        content_revision = result_revision_;
        return acquire_frame(track);
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
        beauty_.submit(std::make_shared<Frame>(std::move(frame)), token);
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
     * Drains application media delivery while the lifecycle executor still accepts callback reentry.
     */
    void shutdown_output() {
        beauty_.shutdown();
        output_.shutdown();
    }

    /**
     * Acquires the latest remote result frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     */
    std::shared_ptr<Frame> acquire_frame(uint64_t revision = 0) {
        auto frame = remote_.acquire(revision);
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
    std::shared_ptr<Frame> acquire_local_frame(uint64_t revision = 0) {
        auto frame = local_.acquire(revision);
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

    /** Returns the configured mirror direction of the result display. */
    bool remote_mirrored() const {
        return remote_mirrored_.load();
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
        if (interaction_enabled) {
            double elapsed = 0;
            const auto strokes = trajectory.effects(elapsed);
            trajectory_renderer_.render(
                window,
                {frame.width, frame.height, 30},
                content_mode,
                strokes,
                elapsed
            );
        }
    }

  private:

    /** Serializes result binding changes from the RTC and lifecycle threads. */
    std::mutex result_binding_;
    /** The active result connection token. */
    uint64_t result_token_ = 0;
    /** Whether the result binding can accept frames. */
    bool result_bound_ = false;
    /** The current result content revision, independent of the stable track identity. */
    uint64_t result_revision_ = 0;

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

    // Declared last so its worker drains before the consumers are destroyed.
    RemoteVideoBeautyPipeline beauty_;
};
} // namespace xmax::detail
