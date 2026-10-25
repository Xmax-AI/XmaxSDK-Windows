//
//  RtcPlatform.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/RTC/RtcCallbacks.hpp"
#include "Foundation/RTC/VideoEncodingConfiguration.hpp"
#include "Foundation/RTC/Connection.hpp"
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

/**
 * The transport contract for room connection, external publication, result reception, and encoding
 * configuration.
 */
class Rtc {

  public:

    /**
     * Releases the RTC adapter after leaving the room and draining callbacks.
     *
     * @note Finish concurrent calls using this object before destroying it.
     */
    virtual ~Rtc() = default;

    /**
     * Initializes native RTC with encoding, audio policy, and callbacks, then waits for room join.
     *
     * @param[in] connection The current RTC room connection information.
     * @param[in] encoding The resolved encoding dimensions, frame rate, bitrate, and degradation policy.
     * @param[in] microphone Whether to start microphone capture when joining the RTC room.
     * @param[in] callbacks Callbacks for result frames, RTC events, failures, and first publication; drained
     * before leave returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    virtual void join(
        const Connection& connection,
        VideoEncodingConfiguration encoding,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel
    ) = 0;

    /**
     * Sends an encoded room-signaling fragment and reports transport failures.
     *
     * @param[in] bytes The encoded bytes to process; they must remain valid until this synchronous call
     * returns.
     */
    virtual void send(
        const std::string& bytes
    ) = 0;

    /**
     * Publishes owned input pixels; may be called on a media thread.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     */
    virtual void push(
        const Frame& frame
    ) = 0;

    /**
     * Resets first-result tracking before a new generation task.
     */
    virtual void begin_generation() = 0;

    /**
     * Waits for a real first result, checking cancellation and running periodic keepalives.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] keepalive The periodic keepalive callback; it must not hold a cross-thread lifecycle lock.
     */
    virtual void wait_remote(
        const Cancel& cancel,
        const std::function<void()>& keepalive
    ) = 0;

    /**
     * Applies a remote playback volume in [0, 1].
     *
     * @param[in] value A finite volume in [0, 1]; 0 mutes playback.
     */
    virtual void volume(
        float value
    ) = 0;

    /**
     * Enables result-user PCM observation without changing playback volume or audio subscription policy.
     *
     * @param[in] enabled Whether to copy remote PCM for application delivery.
     */
    virtual void observe_remote_media(
        bool enabled
    ) = 0;

    /**
     * Applies effective dimensions, frame rate, and bitrate policy.
     *
     * @param[in] value The resolved encoding dimensions, frame rate, bitrate limits, and degradation
     * preference.
     */
    virtual void configure_encoding(
        VideoEncodingConfiguration value
    ) = 0;

    /**
     * Leaves RTC and drains frame, event, and failure callbacks before returning.
     */
    virtual void leave() noexcept = 0;
};

/**
 * The shared native RTC adapter factory; must not return an empty object.
 */
using RtcFactory = std::function<std::shared_ptr<Rtc>()>;

/**
 * Reports whether the current build includes a native RTC backend.
 */
bool rtc_available() noexcept;

/**
 * Creates a native RTC adapter or reports unsupported capability on other platforms.
 *
 * @return A shared native RTC adapter; unsupported platforms throw a capability error.
 */
std::shared_ptr<Rtc> make_rtc();
} // namespace xmax::detail
