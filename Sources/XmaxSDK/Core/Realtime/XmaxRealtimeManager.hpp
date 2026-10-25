//
//  XmaxRealtimeManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/XmaxRealtimeLifecycleManager.hpp"
#include "Service/Realtime/RealtimeTiming.hpp"
#include "Core/Realtime/XmaxRealtimeErrorManager.hpp"
#include "Media/MediaController.hpp"
#include "Render/RenderController.hpp"
#include "Core/Realtime/XmaxRealtimeConnectionManager.hpp"
#include "Core/Realtime/XmaxRealtimeGenerationManager.hpp"
#include "Stream/StreamController.hpp"
#include <optional>

namespace xmax::detail {

/**
 * Manages media bindings, RTC connections, and generation lifecycle on a serialized executor.
 */
class XmaxRealtimeManager {

  public:

    /**
     * Creates an independent realtime lifecycle controller with shared API services and native factories.
     *
     * @param[in] api The shared API service, retained for the controller's lifetime.
     * @param[in] factory The native adapter factory, which must not return an empty object.
     * @param[in] heartbeat The active-session heartbeat interval, in milliseconds.
     * @param[in] camera The native camera adapter factory.
     * @param[in] history Optional quality history scoped by model and direction; an empty reference disables
     * persistence.
     */
    explicit XmaxRealtimeManager(
        std::shared_ptr<ApiService> api,
        RtcFactory factory = make_rtc,
        std::chrono::milliseconds heartbeat = std::chrono::seconds(10),
        CameraFactory camera = make_camera_capture,
        std::shared_ptr<QualityHistory> history = {}
    );

    /**
     * Cancels in-flight work, closes media and RTC resources, and stops lifecycle execution.
     */
    ~XmaxRealtimeManager();

    /**
     * Permanently closes native resources and drains application delivery before stopping lifecycle work.
     * Safe to repeat; source receivers remain inert even while an in-flight call retains this object.
     */
    void shutdown() noexcept;

    /**
     * Binds external video input and enters the media-ready state.
     *
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] has_audio Whether the source provides PCM upload without microphone capture.
     *
     * @return The revision identifying this external source binding.
     */
    uint64_t prepare(
        xmax_video_format format,
        bool has_audio = false
    );

    /**
     * Connects if needed and starts generation, blocking until the first result frame.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] face Whether to enable face restoration for generation.
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     */
    void start(
        std::string prompt,
        std::string reference,
        bool face,
        bool microphone
    );

    /**
     * Replaces the current task's generation conditions.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] face Whether to enable face restoration for generation.
     */
    void update(
        std::string prompt,
        std::string reference,
        bool face
    );

    /**
     * Cancels in-flight work and disconnects, optionally closing local media.
     *
     * @param[in] close Whether to also release local media and cached generation conditions.
     */
    void disconnect(
        bool close
    );

    /**
     * Copies and submits an external frame synchronously; may be called on a media thread.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     */
    void push(
        const xmax_video_frame& frame
    );

    /**
     * Submits a source-bound external frame; replaced or closed bindings silently discard input.
     *
     * @param[in] frame The borrowed pixels, copied before this call returns.
     * @param[in] binding The revision returned when this source was bound.
     */
    void push(
        const xmax_video_frame& frame,
        uint64_t binding
    );

    /**
     * Converts and submits source-bound PCM; stale or disconnected audio is discarded.
     *
     * @param[in] frame The borrowed PCM data on the source video timeline.
     * @param[in] binding The source revision returned at binding.
     *
     * @throws Error If current PCM input is invalid; the binding is also failed and cleaned up.
     */
    void push_audio(
        const xmax_external_audio_frame& frame,
        uint64_t binding
    );

    /**
     * Reports whether a source binding accepts frames for preview and publication.
     *
     * @param[in] binding The revision returned when this source was bound.
     *
     * @return True for a live binding, including while disconnected; false after failure or unbinding.
     */
    bool requesting(
        uint64_t binding
    ) const;

    /**
     * Cancels work using a failed external source and schedules media, connection, and session cleanup.
     *
     * @param[in] binding The source revision; stale or already-failed bindings are ignored.
     * @param[in] error The source failure, copied and redacted before application delivery.
     */
    void external_failure(
        uint64_t binding,
        Error error
    );

    /**
     * Starts a camera, waits for its first frame, and enters ready state with native capture dimensions
     * preserved.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     */
    void prepare_camera(
        std::string device,
        xmax_video_format format,
        bool microphone
    );

    /**
     * Switches the active camera and resumes generation with retained conditions.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     */
    void switch_camera(
        std::string device
    );

    /**
     * Releases local capture and input bindings after disconnection.
     */
    void stop_media();

    /**
     * Creates a server session and joins RTC without starting generation.
     *
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     */
    void connect(
        bool microphone
    );

    /**
     * Starts a task with supplied or cached conditions, blocking until the first result frame.
     *
     * @param[in] value Optional generation conditions; null reuses previous conditions, but the first call
     * requires them.
     */
    void generate(
        const xmax_generation_context* value
    );

    /**
     * Returns the session state, session identifier, and task identifier.
     */
    xmax_event state_snapshot();

    /**
     * Returns the currently bound input video format.
     */
    xmax_video_format format();

    /**
     * Returns the remote playback volume for the current or next connection.
     *
     * @return The playback volume in [0, 1], initially 0.
     */
    float volume();

    /**
     * Configures frame interpolation; enabling requires a native backend.
     *
     * @param[in] enabled True to enable; false to disable.
     */
    void interpolation(
        bool enabled
    );

    /**
     * Validates and applies uplink bitrate limits and degradation preference.
     *
     * @param[in] value The uplink bitrate limits and degradation preference; see xmax_encoding_config for
     * units and special values.
     */
    void encoding(
        xmax_encoding_config value
    );

    /**
     * Commits RTC publication mirroring before display settings, retaining the previous configuration on
     * failure.
     *
     * @param[in] value The local preview, remote display, and published-video mirror settings.
     */
    void video_mirror(
        xmax_video_mirror_config value
    );

    /**
     * Returns the mirror configuration retained for the current and next connection.
     */
    xmax_video_mirror_config video_mirror();

    /**
     * Enables or disables quality adaptation and resets the network observation window.
     *
     * @param[in] enabled True to enable; false to disable.
     */
    void adaptive(
        bool enabled
    );

    /**
     * Returns the current uplink and downlink quality levels and effective video formats.
     *
     * @return The adaptation setting, directional quality levels, and effective dimensions and frame rates.
     */
    xmax_quality_status quality();

    /**
     * Reports whether external input accepts preview and publication frames.
     *
     * @return True for a live external binding, including while disconnected; false for camera mode or failed input.
     */
    bool requesting() const;

    /**
     * Sets and retains the remote playback volume for the current or next connection.
     *
     * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
     */
    void volume(
        float value
    );

    /**
     * Replaces the serialized remote audio/video listener, preserving playback and the current listener
     * if native audio observation cannot be enabled.
     *
     * @param[in] listener The owned-packet callback, or an empty function to discard queued output and stop
     * observation. An in-flight callback may finish after this method returns.
     */
    void media_listener(
        RemoteMediaListener listener
    );

    /**
     * Consumes the oldest pending application event without blocking.
     *
     * @param[out] event Receives the oldest pending event; unchanged if the queue is empty.
     *
     * @return True if an event was retrieved; false if no event is pending.
     */
    bool poll(
        xmax_event& event
    ) {
        return lifecycle_.poll(event);
    }

    /**
     * Acquires the latest remote result frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     */
    std::shared_ptr<Frame> acquire_frame() {
        return render_.acquire_frame();
    }

    /**
     * Acquires the latest local preview frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     */
    std::shared_ptr<Frame> acquire_local_frame() {
        return render_.acquire_local_frame();
    }

    /**
     * Updates a ready, disconnected camera's encoding format, attempting to restore the previous format on
     * failure.
     *
     * @param[in] format The target dimensions and frame rate supported by the model.
     */
    void update_camera_format(
        xmax_video_format format
    );

    /**
     * Returns the camera format, or an all-zero format if no camera stream exists.
     */
    xmax_video_format camera_format();

    /**
     * Returns the latest directional video statistics without consuming events.
     *
     * @param[in] direction Zero for uplink and one for downlink.
     *
     * @return A current snapshot, or available = 0 with unavailable metrics set to -1.
     */
    xmax_video_statistics video_statistics(
        int direction
    ) const;

    /**
     * Applies a directional quality level during generation without locking out subsequent adaptation.
     *
     * @param[in] direction 0 for uplink; 1 for downlink.
     * @param[in] level The target quality level, in [1, 5].
     */
    void quality_level(
        int direction,
        int level
    );

    /**
     * Returns the current state snapshot with its reason and error.
     */
    xmax_event_info state_info();

    /**
     * Acquires an event with state reason and error details without blocking.
     *
     * @param[out] info Receives the oldest pending event.
     *
     * @return Whether an event was retrieved.
     */
    bool poll(
        xmax_event_info& info
    ) {
        return lifecycle_.poll(info);
    }

    /**
     * Disconnects for the specified reason while retaining local media.
     *
     * @param[in] reason The normal-stop or orientation-change reason.
     */
    void disconnect_reason(
        xmax_realtime_reason reason
    );

    /**
     * Copies viewport trajectory samples and schedules asynchronous delivery, retaining only the latest
     * pending sample.
     *
     * @param[in] frame The sampled points and actual video layout.
     */
    void submit_interaction(
        const xmax_interaction_frame& frame
    );

    /**
     * Returns a copy of unexpired normalized points for the current task.
     */
    std::vector<xmax_point> trajectory() const {
        if (lifecycle_.state() != XMAX_GENERATING)
            return {};
        return render_.trajectory.snapshot();
    }

    /**
     * Returns the model capabilities used by this realtime controller.
     */
    xmax_model_info model_info() const {
        return RealtimeModelDefinition::info(RealtimeModelDefinition::parse(api_->config().model));
    }

    /**
     * Delegates result-frame and interaction-effect presentation to the render layer.
     *
     * @param[in] frame The result frame to display, valid throughout the call.
     * @param[in] window An HWND owned by the calling thread.
     * @param[in] content_mode Aspect fit or centered aspect fill.
     */
    void render_window(
        const Frame& frame,
        void* window,
        int32_t content_mode
    ) const {
        render_.render_window(
            frame,
            window,
            content_mode,
            lifecycle_.state() == XMAX_GENERATING
        );
    }

  private:

    /**
     * Retains the shared API service so child controllers survive destruction of the client handle.
     */
    std::shared_ptr<ApiService> api_;

    /**
     * The service managing server-session creation, keepalive, and closing.
     */
    XmaxRealtimeConnectionManager connection_;

    /**
     * The controller for RTC publication, generation signaling, and quality policy.
     */
    StreamController stream_;

    /**
     * The controller for native media and external input bindings.
     */
    MediaController media_;

    /**
     * The controller retaining the latest local preview and remote result frames.
     */
    RenderController render_;

    /**
     * The lifecycle-queue-owned mirror configuration, retained after disconnection.
     */
    xmax_video_mirror_config mirror_{
        sizeof(xmax_video_mirror_config),
        0,
        0,
        0
    };

    /**
     * The media-binding revision used to reject old callbacks.
     */
    std::atomic<uint64_t> media_epoch_{0};

    /**
     * Serializes binding revision changes with asynchronous fault admission.
     */
    mutable std::mutex media_binding_mutex_;

    /**
     * The failed binding, read atomically by source callbacks; first-fault acceptance is serialized.
     */
    std::atomic<uint64_t> failed_media_epoch_{0};

    /**
     * The microphone connection policy saved with the camera binding.
     */
    bool camera_microphone_ = false;

    /**
     * The mutex protecting RTC-thread statistics updates and application snapshot reads.
     */
    mutable std::mutex statistics_mutex_;

    /**
     * The latest uplink and downlink statistics; available = 0 marks a cleared stream.
     */
    xmax_video_statistics statistics_[2]{};

    /**
     * The manager for generation conditions and task trajectories.
     */
    XmaxRealtimeGenerationManager generation_;

    /**
     * The conditions type used by the generation manager.
     */
    using Context = XmaxRealtimeGenerationManager::Context;

    /**
     * The component retaining the lifecycle state's reason and original error.
     */
    XmaxRealtimeErrorManager errors_;

    /**
     * The stage timer for connection and generation startup.
     */
    RealtimeTiming timing_;

    /**
     * The active-session heartbeat interval as a millisecond duration.
     */
    std::chrono::milliseconds heartbeat_interval_;

    /**
     * The owner of operation admission, scoped termination, state, and application events.
     */
    XmaxRealtimeLifecycleManager lifecycle_;

    /**
     * Ensures final shutdown finishes once, independently of temporary source-call references.
     */
    std::once_flag shutdown_once_;

    /**
     * The mutex protecting the interaction task identity read by UI threads.
     */
    std::mutex interaction_mutex_;

    /**
     * The task accepting interaction, or empty when generation is inactive.
     */
    std::string interaction_task_;

    /**
     * Replaces an unfinished generation request, then executes after its cleanup on the lifecycle queue.
     *
     * @param[in] context Conditions to commit on success; an empty value reuses the last committed conditions.
     * @param[in] microphone Whether to enable microphone capture if a connection must be created.
     */
    void request_generation(
        std::optional<Context> context,
        bool microphone
    );

    /**
     * Schedules one trajectory sample fairly so continuous input cannot monopolize lifecycle execution.
     */
    void drain_interaction();

    /**
     * Publishes a timing snapshot with the current lifecycle state and session/task identities.
     *
     * @param[in] event The completed or failed startup timing snapshot.
     */
    void emit_timing(
        xmax_event event
    );

    /**
     * Updates session state and delivers its state event to your app.
     *
     * @param[in] state The new realtime lifecycle state.
     */
    void change_state(
        xmax_state state
    );

    /**
     * Stages final state and startup bookkeeping until the operation releases admission.
     *
     * @param[in] state The operation's successful phase.
     * @param[in] token The current operation token.
     * @param[in] completed Optional bookkeeping performed after state publication.
     */
    void complete_state(
        xmax_state state,
        const XmaxRealtimeLifecycleManager::Token& token,
        std::function<void()> completed = {}
    );

    /**
     * Releases the resource scope selected by lifecycle termination without owning state transitions.
     *
     * @param[in] scope The generation, connection, or full media cleanup scope.
     *
     * @return The remaining local-media availability, session identifier, and release outcome.
     */
    XmaxRealtimeLifecycleManager::CleanupResult cleanup(
        XmaxRealtimeLifecycleManager::TerminationScope scope
    );

    /**
     * Runs due server-session and RTC room keepalives for an active connection.
     */
    void heartbeat();

    /**
     * Checks cancellation and keeps the session and room alive while waiting for generation.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void keepalive(
        const Cancel& cancel
    );

    /**
     * Releases resources held by a failed operation and delivers the original SDK error.
     *
     * @param[in] error The original SDK exception whose code and diagnostic must be preserved.
     */
    void fail(
        Error error
    );

    /**
     * Creates a server session and joins RTC, rolling back allocated resources on cancellation or failure.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] epoch The cancellation revision captured when the operation starts, used to reject stale
     * work.
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     */
    void connect_impl(
        const Cancel& cancel,
        uint64_t epoch,
        bool microphone
    );

    /**
     * Sends generation conditions, enables input delivery, and waits for the first result frame.
     *
     * @param[in] context The prompt, reference image, and face-restoration conditions for this generation.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     *
     * @return True when a new task reached its first result; false when existing conditions were updated.
     */
    bool generate_impl(
        const Context& context,
        const Cancel& cancel
    );

    /**
     * Binds local preview to a new media revision, rejecting earlier frames.
     */
    void bind_local();

    /**
     * Invalidates local source callbacks and clears preview after media has stopped.
     */
    void unbind_local();

    /**
     * Creates a failure callback that accepts only the current media binding's errors.
     *
     * @param[in] token The connection or media-binding revision used to reject stale callbacks.
     *
     * @return The current binding's failure callback; errors from previous bindings are ignored.
     */
    std::function<void(Error)> media_failure(
        uint64_t token
    );
};
} // namespace xmax::detail
