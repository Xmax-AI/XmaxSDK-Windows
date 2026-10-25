//
//  realtime.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "handle_owner.hpp"
#include "error.hpp"
#include "frame.hpp"
#include "media_frame.hpp"
#include "external_frame_sink.hpp"
#include <functional>
#include <memory>
#include "RealtimeCameraPreset.hpp"
#include "RealtimeQualityLevel.hpp"
#include <string>
#include <vector>

namespace xmax {

/**
 * Manages media input, RTC connection, and generation; call blocking operations on worker threads.
 */
class realtime : public detail::owner<xmax_realtime, xmax_realtime_destroy> {

  public:

    /**
     * Inherits the handle owner's constructors; an empty handle creates an empty owner.
     *
     * @note The underlying resource is not copied. Destruction invokes its matching release function.
     */
    using owner::owner;

    /**
     * Binds external input supplied by your app.
     *
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_prepare_external
     */
    void prepare(
        xmax_video_format format
    ) {
        check(xmax_realtime_prepare_external(
            get(),
            &format
        ));
    }

    /**
     * Binds caller-owned video and returns a receiver scoped to this source's frames and errors.
     *
     * @param[in] format The exact external input dimensions and frame rate.
     *
     * @return A move-only receiver; older source receivers become stale after replacement or closure.
     *
     * @see xmax_realtime_bind_external_video
     */
    external_frame_sink bind_external(
        xmax_video_format format
    ) {
        xmax_external_frame_sink result = nullptr;
        check(xmax_realtime_bind_external_video(
            get(),
            &format,
            &result
        ));
        return external_frame_sink(result);
    }

    /**
     * Binds caller-owned video with optional PCM, leaving capture ownership with your app.
     *
     * @param[in] config The fixed source format and audio declaration.
     *
     * @return A source-scoped receiver accepting preview frames even while disconnected.
     *
     * @see xmax_realtime_bind_external_media
     */
    external_frame_sink bind_external_media(
        const xmax_external_media_config& config
    ) {
        xmax_external_frame_sink result = nullptr;
        check(xmax_realtime_bind_external_media(
            get(),
            &config,
            &result
        ));
        return external_frame_sink(result);
    }

    /**
     * Returns independent native capture, encoding, transmission, decoding, and rendering metrics.
     *
     * @param[in] direction Zero for uplink and one for downlink.
     *
     * @return The latest snapshot; available = 0 before statistics arrive or after disconnection.
     */
    xmax_video_statistics video_statistics(
        int32_t direction
    ) const {
        xmax_video_statistics result{};
        result.struct_size = sizeof(result);
        check(xmax_realtime_get_video_statistics(
            get(),
            direction,
            &result
        ));
        return result;
    }

    /**
     * Sets one serialized callback for result-user PCM and decoded video on a shared rendering timeline.
     *
     * @param[in] listener The background callback, or an empty function to discard queued output and stop
     * observation. Each invocation owns its packet and may move it elsewhere for asynchronous use.
     *
     * @note Registration preserves playback volume and subscriptions. Replacing a listener may leave an
     * in-flight callback running; captured objects stay alive until that invocation finishes. Forward
     * packets promptly to avoid the bounded queue dropping later output.
     *
     * @see xmax_realtime_set_remote_media_frame_listener
     */
    void remote_media_listener(
        std::function<void(media_frame)> listener
    ) {
        if (!listener) {
            check(xmax_realtime_set_remote_media_frame_listener(
                get(),
                nullptr
            ));
            return;
        }

        auto context = std::make_unique<std::function<void(media_frame)>>(std::move(listener));
        const xmax_remote_media_listener registration{
            sizeof(registration),
            [](void* user_data, xmax_media_frame borrowed) {
                xmax_media_frame retained = nullptr;
                check(xmax_media_frame_retain(
                    borrowed,
                    &retained
                ));
                (*static_cast<std::function<void(media_frame)>*>(user_data))(media_frame(retained));
            },
            context.get(),
            [](void* user_data) { delete static_cast<std::function<void(media_frame)>*>(user_data); }
        };
        check(xmax_realtime_set_remote_media_frame_listener(
            get(),
            &registration
        ));
        context.release();
    }

    /**
     * Starts SDK camera capture and waits for the first real frame.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_prepare_camera
     */
    void prepare_camera(
        const std::string& device = {},
        xmax_video_format format = {},
        bool microphone = false
    ) {
        const xmax_camera_config config{
            sizeof(config),
            device.empty() ? nullptr : device.c_str(),
            format,
            microphone
        };
        check(xmax_realtime_prepare_camera(
            get(),
            &config
        ));
    }

    /**
     * Switches the active camera while retaining the connection and generation conditions.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_switch_camera
     */
    void switch_camera(
        const std::string& device
    ) {
        check(xmax_realtime_switch_camera(
            get(),
            device.c_str()
        ));
    }

    /**
     * Releases local capture and input bindings after disconnection.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_stop_media
     */
    void stop_media() {
        check(xmax_realtime_stop_media(get()));
    }

    /**
     * Creates a server session and joins RTC without starting generation.
     *
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_connect
     */
    void connect(
        bool microphone = false
    ) {
        check(xmax_realtime_connect(
            get(),
            microphone
        ));
    }

    /**
     * Connects and starts generation, blocking until the first result frame.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     * @param[in] face Whether to enable face restoration for generation.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_start
     */
    void start(
        const std::string& prompt,
        const std::string& reference = {},
        bool microphone = false,
        bool face = false
    ) {
        const xmax_generation_context context{
            sizeof(context),
            prompt.c_str(),
            reference.empty() ? nullptr : reference.c_str(),
            face
        };
        check(xmax_realtime_start(
            get(),
            &context,
            microphone
        ));
    }

    /**
     * Starts generation on an existing connection using cached conditions.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_generate
     */
    void generate() {
        check(xmax_realtime_generate(
            get(),
            nullptr
        ));
    }

    /**
     * Starts generation on an existing connection using supplied conditions.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] face Whether to enable face restoration for generation.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_generate
     */
    void generate(
        const std::string& prompt,
        const std::string& reference = {},
        bool face = false
    ) {
        const xmax_generation_context context{
            sizeof(context),
            prompt.c_str(),
            reference.empty() ? nullptr : reference.c_str(),
            face
        };
        check(xmax_realtime_generate(
            get(),
            &context
        ));
    }

    /**
     * Replaces the active task's generation conditions.
     *
     * @param[in] prompt The generation prompt, encoded as UTF-8.
     * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
     * @param[in] face Whether to enable face restoration for generation.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_update
     */
    void update(
        const std::string& prompt,
        const std::string& reference = {},
        bool face = false
    ) {
        const xmax_generation_context context{
            sizeof(context),
            prompt.c_str(),
            reference.empty() ? nullptr : reference.c_str(),
            face
        };
        check(xmax_realtime_update(
            get(),
            &context
        ));
    }

    /**
     * Reports whether the current external source accepts preview or publication frames.
     *
     * @return True for a live external binding, including while disconnected; false for camera mode or failed input.
     *
     * @see xmax_realtime_is_requesting_frames
     */
    bool requesting_frames() const {
        int32_t result = 0;
        check(xmax_realtime_is_requesting_frames(
            get(),
            &result
        ));
        return result != 0;
    }

    /**
     * Copies and submits an external frame synchronously; your app may reuse source pixels after return.
     *
     * @param[in] value The external view with struct_size initialized, matching bound dimensions and a
     * strictly increasing timestamp.
     *
     * @see xmax_realtime_push_video_frame
     */
    void push(
        const xmax_video_frame& value
    ) {
        check(xmax_realtime_push_video_frame(
            get(),
            &value
        ));
    }

    /**
     * Sets and retains the remote playback volume for the current or next connection.
     *
     * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
     *
     * @note Independent of connection and generation operations. The setting is retained before
     * result-user audio is available and applied before audio subscription starts. Set after preparing the
     * local stream to override its initial playback volume.
     *
     * @see xmax_realtime_set_remote_volume
     */
    void volume(
        float value
    ) {
        check(xmax_realtime_set_remote_volume(
            get(),
            value
        ));
    }

    /**
     * Returns the remote playback volume for the current or next connection.
     *
     * @return The playback volume in [0, 1], initially 0.
     *
     * @see xmax_realtime_get_remote_volume
     */
    float volume() const {
        float value = 0;
        check(xmax_realtime_get_remote_volume(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Returns the current state snapshot without consuming pending events.
     *
     * @return The current state and session identifier snapshot.
     *
     * @see xmax_realtime_get_state
     */
    xmax_event state() const {
        xmax_event value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_state(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Returns the currently bound input video format.
     *
     * @see xmax_realtime_get_format
     */
    xmax_video_format format() const {
        xmax_video_format value{};
        check(xmax_realtime_get_format(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Consumes the next application event without blocking.
     *
     * @param[out] event Receives the oldest pending event; this method initializes struct_size.
     *
     * @return True if an event was retrieved; false if no event is pending.
     *
     * @see xmax_realtime_poll_event
     */
    bool poll(
        xmax_event& event
    ) const {
        event.struct_size = sizeof(event);
        const auto result = xmax_realtime_poll_event(
            get(),
            &event
        );
        check(result);
        return result == XMAX_OK;
    }

    /**
     * Acquires the latest remote result frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     *
     * @see xmax_realtime_acquire_frame
     */
    frame acquire_frame() const {
        xmax_frame value = nullptr;
        check(xmax_realtime_acquire_frame(
            get(),
            &value
        ));
        return frame(value);
    }

    /**
     * Acquires the latest local preview frame without blocking, transferring ownership.
     *
     * @return A frame that retains its pixels, or an empty object if no new frame is available.
     *
     * @see xmax_realtime_acquire_local_frame
     */
    frame acquire_local_frame() const {
        xmax_frame value = nullptr;
        check(xmax_realtime_acquire_local_frame(
            get(),
            &value
        ));
        return frame(value);
    }

    /**
     * Sets uplink bitrate limits and quality or frame-rate degradation preference.
     *
     * @param[in] minimum The minimum bitrate in kbps; -1 selects the policy default and 0 omits the minimum
     * limit.
     * @param[in] maximum The maximum bitrate in kbps; 0 uses the policy estimate.
     * @param[in] preference The XMAX_ENCODER_* degradation preference.
     *
     * @see xmax_realtime_set_encoding
     */
    void encoding(
        int minimum = -1,
        int maximum = 0,
        int preference = XMAX_ENCODER_MAINTAIN_FRAMERATE
    ) {
        const xmax_encoding_config value{
            sizeof(value),
            minimum,
            maximum,
            preference
        };
        check(xmax_realtime_set_encoding(
            get(),
            &value
        ));
    }

    /**
     * Sets preview, result-display, and publication mirroring independently, retaining settings after
     * disconnection.
     *
     * @param[in] local_preview Whether to horizontally mirror local preview.
     * @param[in] remote_view Whether to horizontally mirror result display.
     * @param[in] published_video Whether RTC mirrors uplink encoding; disabled by default.
     *
     * @see xmax_realtime_set_video_mirror
     */
    void video_mirror(
        bool local_preview,
        bool remote_view,
        bool published_video = false
    ) const {
        const xmax_video_mirror_config value{
            sizeof(value),
            local_preview,
            remote_view,
            published_video
        };
        check(xmax_realtime_set_video_mirror(
            get(),
            &value
        ));
    }

    /**
     * Returns the mirror configuration retained for the current and next connection.
     *
     * @see xmax_realtime_get_video_mirror
     */
    xmax_video_mirror_config video_mirror() const {
        xmax_video_mirror_config value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_video_mirror(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Enables or disables independent uplink and downlink quality adaptation.
     *
     * @param[in] enabled True to enable; false to disable.
     *
     * @see xmax_realtime_set_adaptive_quality
     */
    void adaptive_quality(
        bool enabled
    ) {
        check(xmax_realtime_set_adaptive_quality(
            get(),
            enabled
        ));
    }

    /**
     * Returns the current uplink and downlink quality levels and effective video formats.
     *
     * @return The adaptation setting, directional quality levels, and effective dimensions and frame rates.
     *
     * @see xmax_realtime_get_quality
     */
    xmax_quality_status quality() const {
        xmax_quality_status value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_quality(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Closes server and RTC resources while retaining local preview and cached conditions.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_disconnect
     */
    void disconnect() {
        check(xmax_realtime_disconnect(get()));
    }

    /**
     * Disconnects and releases local media and cached generation conditions.
     *
     * @note This call blocks. Call it on a worker thread; failures throw xmax::error.
     *
     * @see xmax_realtime_close
     */
    void close() {
        check(xmax_realtime_close(get()));
    }

    /**
     * Updates a ready, disconnected camera's encoding format, retaining device and preview bindings.
     *
     * @param[in] format The model-supported new format.
     */
    void update_camera_format(
        xmax_video_format format
    ) {
        check(xmax_realtime_update_camera_format(
            get(),
            &format
        ));
    }

    /**
     * Updates camera resolution while retaining frame rate, bitrate, and encoding preference.
     *
     * @param[in] preset The model-supported camera resolution.
     */
    void update_camera_format(
        RealtimeCameraPreset preset
    ) {
        check(xmax_realtime_update_camera_preset(
            get(),
            static_cast<xmax_camera_preset>(preset)
        ));
    }

    /**
     * Returns the camera encoding format, or an all-zero format without a camera stream.
     */
    xmax_video_format camera_format() const {
        xmax_video_format result{};
        xmax_camera_preset preset{};
        check(xmax_realtime_get_camera_format(
            get(),
            &result,
            &preset
        ));
        return result;
    }

    /**
     * Returns the current camera preset, or none without a camera stream.
     */
    RealtimeCameraPreset camera_preset() const {
        xmax_video_format format{};
        xmax_camera_preset result{};
        check(xmax_realtime_get_camera_format(
            get(),
            &format,
            &result
        ));
        return static_cast<RealtimeCameraPreset>(result);
    }

    /**
     * Changes uplink quality during generation without disabling subsequent network adaptation.
     *
     * @param[in] level The target uplink quality level.
     */
    void uplink_quality_level(
        RealtimeQualityLevel level
    ) {
        check(xmax_realtime_set_uplink_quality_level(
            get(),
            static_cast<int32_t>(level)
        ));
    }

    /**
     * Changes downlink quality during generation, retaining the previous level on send failure.
     *
     * @param[in] level The target downlink quality level.
     */
    void downlink_quality_level(
        RealtimeQualityLevel level
    ) {
        check(xmax_realtime_set_downlink_quality_level(
            get(),
            static_cast<int32_t>(level)
        ));
    }

    /**
     * Returns a state snapshot with reason and error details.
     */
    xmax_event_info state_info() const {
        xmax_event_info info{};
        info.struct_size = sizeof(info);
        check(xmax_realtime_get_state_info(
            get(),
            &info
        ));
        return info;
    }

    /**
     * Acquires an event with reason and error details without blocking, sharing the legacy poll queue.
     *
     * @param[out] info Receives the oldest event; unchanged if the queue is empty.
     *
     * @return Whether an event was retrieved.
     */
    bool poll(
        xmax_event_info& info
    ) {
        info.struct_size = sizeof(info);
        const auto result = xmax_realtime_poll_event_info(
            get(),
            &info
        );
        check(result);
        return result == XMAX_OK;
    }

    /**
     * Disconnects for the specified reason while retaining local media preview.
     *
     * @param[in] reason Normal stop or display-orientation change.
     */
    void disconnect(
        xmax_realtime_reason reason
    ) {
        check(xmax_realtime_disconnect_with_reason(
            get(),
            reason
        ));
    }

    /**
     * Submits trajectory samples for SDK coordinate mapping and asynchronous signaling.
     *
     * @param[in] frame The sample points, viewport dimensions, and actual video layout.
     */
    void submit_interaction(
        const xmax_interaction_frame& frame
    ) {
        check(xmax_realtime_submit_interaction(
            get(),
            &frame
        ));
    }

    /**
     * Returns unexpired normalized trajectories for custom rendering.
     */
    std::vector<xmax_point> trajectory() const {
        std::vector<xmax_point> points(256);
        size_t count = 0;
        check(xmax_realtime_get_trajectory(
            get(),
            points.data(),
            points.size(),
            &count
        ));
        points.resize(count);
        return points;
    }

    /**
     * Renders a frame and default trajectory effects on the HWND owner thread.
     *
     * @param[in] value The video frame to display.
     * @param[in] window An HWND owned by the calling thread.
     * @param[in] content_mode Aspect fit or centered aspect fill.
     */
    void render_window(
        xmax_frame value,
        void* window,
        int32_t content_mode = XMAX_CONTENT_FIT
    ) {
        check(xmax_realtime_render_window(
            get(),
            value,
            window,
            content_mode
        ));
    }
};
} // namespace xmax
