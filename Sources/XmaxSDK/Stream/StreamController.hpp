//
//  StreamController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Quality/QualityController.hpp"
#include "Stream/Quality/QualityHistory.hpp"
#include "Stream/Room/RoomEvent.hpp"
#include "Stream/Room/RoomController.hpp"
#include "Stream/Encoding/EncodingController.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * Manages RTC publication, generation signaling, and directional quality; lifecycle work is serialized and
 * frames may arrive on media threads. Playback volume is synchronized independently of lifecycle work.
 */
class StreamController {

  public:

    /**
     * Retains the native RTC factory, optional quality-history backend, and model pixel floor.
     */
    explicit StreamController(
        RtcFactory factory = make_rtc,
        std::shared_ptr<QualityHistory> history = {},
        std::chrono::milliseconds heartbeat_interval = std::chrono::seconds(10),
        uint32_t uplink_minimum_pixels = default_uplink_minimum_pixels
    )
        : room_(std::move(factory), heartbeat_interval),
          history_(std::move(history)),
          encoding_(uplink_minimum_pixels) {
    }

    /**
     * Leaves RTC, drains callbacks, and releases publication and signaling resources.
     */
    ~StreamController();

    /**
     * Creates the native RTC adapter before allocating a server session.
     */
    void allocate();

    /**
     * Joins the room with input encoding, audio policy, and lifecycle-protected callbacks.
     *
     * @param[in] connection The current RTC room connection information.
     * @param[in] format The requested pixel dimensions and frame rate, subject to the interface's format
     * limits.
     * @param[in] publish_audio Whether to publish a custom PCM track on join.
     * @param[in] callbacks Callbacks for result frames, RTC events, failures, and first publication; drained
     * before stop returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] generation_format The generation canvas; zero dimensions select the input format.
     */
    void join(
        const Connection& connection,
        xmax_video_format format,
        bool publish_audio,
        RtcCallbacks callbacks,
        const Cancel& cancel,
        xmax_video_format generation_format = {}
    );

    /**
     * Configures the session-scoped HTTP command relay before joining RTC.
     */
    void event_relay(
        RoomCommandController::Relay relay
    ) {
        room_.event_relay(std::move(relay));
    }

    /**
     * Creates a task and sends start signaling before the startup deadline.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] face Whether to enable face restoration for generation.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] deadline The startup deadline shared with remote publication confirmation.
     */
    void start(
        const std::string& prompt,
        const std::string& reference,
        bool face,
        const Cancel& cancel,
        Clock::time_point deadline = Clock::time_point::max()
    );

    /**
     * Waits for the selected result user to publish video after the start signal.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] deadline The startup deadline shared with the start send.
     */
    void wait_remote(const Cancel& cancel, Clock::time_point deadline);

    /** Starts result audio once a processed result frame is available. */
    void activate_result_audio();

    /**
     * Sends updated generation conditions using the current task identifier.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] face Whether to enable face restoration for generation.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] maintain The maintenance callback invoked while waiting or throttled; empty omits
     * maintenance.
     */
    void update(
        const std::string& prompt,
        const std::string& reference,
        bool face,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    );

    /**
     * Sends RTC room keepalive signaling for the current task.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void heartbeat(
        const Cancel& cancel
    );

    /**
     * Disconnects media-thread RTC access, leaves the room, and clears signaling state.
     */
    void stop() noexcept;

    /**
     * Publishes a frame using a thread-safe snapshot of the active RTC adapter.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     */
    void push(
        const Frame& frame
    );

    /** Changes custom PCM publication without changing the active media source. */
    void audio_publication(bool enabled) {
        if (room_.joined())
            room_.backend()->audio_publication(enabled);
    }

    /**
     * Publishes local PCM through the active room snapshot.
     *
     * @param[in] frame The 10 ms PCM packet on the source video timeline.
     */
    void push_audio(
        const ExternalAudioPacket& frame
    ) {
        room_.push_audio(frame);
    }

    /**
     * Sets and retains the remote playback volume for the current or next connection.
     *
     * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
     *
     * @note Safe during connection, generation, and disconnect. Native adapters retain the value until
     * result-user audio becomes available.
     */
    void volume(
        float value
    );

    /**
     * Returns the remote playback volume for the current or next connection.
     *
     * @return The playback volume in [0, 1], initially 0.
     */
    float volume() const {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        return volume_;
    }

    /**
     * Stops generation while retaining the RTC connection.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void stop_generation(
        const Cancel& cancel
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

    /** Returns the current encoder overrides. */
    xmax_encoding_config encoding() const { return encoding_.config(); }

    /**
     * Retains or updates uplink mirroring, applied by the RTC encoder when connected.
     *
     * @param[in] enabled Whether to horizontally mirror the published video.
     */
    void mirror(
        bool enabled
    ) {
        encoding_.mirror(
            enabled,
            format_,
            quality_.up.level,
            room_.joined() ? room_.backend() : nullptr
        );
    }

    /**
     * Enables or disables quality adaptation and resets the network observation window.
     *
     * @param[in] enabled True to enable; false to disable.
     */
    void adaptive(
        bool enabled
    ) {
        quality_.enabled = enabled;
        quality_.reset_samples();
    }

    /**
     * Configures remote PCM observation for the active RTC adapter, or retains it for the next join.
     *
     * @param[in] enabled Whether application output requires result-user audio packets.
     */
    void observe_remote_media(
        bool enabled
    ) {
        if (room_.joined())
            room_.backend()->observe_remote_media(enabled);
        remote_media_enabled_ = enabled;
    }

    /**
     * Returns the current uplink and downlink quality levels and effective video formats.
     *
     * @return The adaptation setting, directional quality levels, and effective dimensions and frame rates.
     */
    xmax_quality_status quality() const;

    /**
     * Updates directional network-quality policy and returns resulting quality or performance events.
     *
     * @param[in] direction The transport direction: 0 for uplink or 1 for downlink.
     * @param[in] network_quality The native network-quality value for this direction.
     * @param[in] sampled_at The sample's monotonic time in seconds; expired or duplicate samples are
     * ignored.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     *
     * @return The events triggered by this observation, or an empty collection if unchanged.
     */
    std::vector<xmax_event>
    observe_quality(
        int direction,
        int network_quality,
        double sampled_at,
        const Cancel& cancel
    );

    /**
     * Applies your app's directional level, preserving the previous level on failure.
     *
     * @param[in] direction 0 for uplink; 1 for downlink.
     * @param[in] level The target quality level, in [1, 5].
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] maintain The keepalive callback invoked while waiting to send signaling.
     *
     * @return The committed quality-change event.
     */
    xmax_event
    set_quality(
        int direction,
        int level,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    );

    /**
     * Sends normalized trajectories for the current generation task.
     *
     * @param[in] points The normalized points.
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] maintain The keepalive callback invoked while waiting to send signaling.
     */
    void tracks(
        const std::vector<xmax_point>& points,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    ) {
        require(
            room_.joined() && !room_.task.empty(),
            XMAX_INVALID_STATE,
            "No generation is active"
        );
        room_.tracks(
            points,
            cancel,
            maintain
        );
    }

    /**
     * Returns a borrowed reference to the active task identifier.
     *
     * @return The task identifier, valid until the controller is destroyed or its task changes.
     */
    const std::string& task() const {
        return room_.task;
    }

  private:

    xmax_video_format quality_at(int level, bool downlink) const {
        return downlink ? quality_format(format_, level, true) : encoding_.format(format_, level);
    }

    /**
     * The room lifecycle, publication entry point, and throttled signaling channel.
     */
    RoomController room_;

    /**
     * The external input format or camera encoding target used as the directional quality baseline.
     */
    xmax_video_format format_{};

    /**
     * The model-resolved generation canvas, independent of capture and encoding dimensions.
     */
    xmax_video_format generation_format_{};

    /**
     * The remote playback volume in [0, 1] for the current or next connection.
     */
    float volume_ = 0;

    /**
     * Protects playback configuration and its adapter binding independently of lifecycle waits.
     */
    mutable std::mutex playback_mutex_;

    /**
     * The allocated playback adapter, detached before room leave starts draining native callbacks.
     */
    std::shared_ptr<Rtc> playback_rtc_;

    /**
     * The uplink and downlink quality policies and adaptation setting.
     */
    QualityController quality_;

    /**
     * The shared valid quality history scoped by model and direction.
     */
    std::shared_ptr<QualityHistory> history_;

    /**
     * The last quality-cache write time for each direction, on the monotonic clock.
     */
    Clock::time_point quality_saved_[2]{};

    /**
     * The last levels committed while adaptation was enabled, used when persistence is not configured.
     */
    int cached_quality_[2]{2, 2};

    /**
     * Whether this controller's output listener requires native result-user PCM observation.
     */
    bool remote_media_enabled_ = false;

    /**
     * The uplink bitrate limits and degradation preference specified by your app.
     */
    EncodingController encoding_;

    /**
     * Resolves native encoding parameters from the quality level and bitrate overrides.
     *
     * @param[in] level The quality level, in [1, 5].
     */
    VideoEncodingConfiguration uplink_encoding(
        int level
    ) const;

    /**
     * Builds signaling from the active room, task identifier, and generation conditions.
     *
     * @param[in] event The room signaling event name.
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference An optional generation reference image path or URL.
     * @param[in] face Whether to enable face restoration for generation.
     *
     * @return The generation event JSON ready for room signaling.
     */
    json generation_message(
        const std::string& event,
        const std::string& prompt,
        const std::string& reference,
        bool face
    ) const;
};
} // namespace xmax::detail
