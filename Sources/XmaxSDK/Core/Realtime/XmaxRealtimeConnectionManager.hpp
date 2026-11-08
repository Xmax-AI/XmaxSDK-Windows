//
//  XmaxRealtimeConnectionManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Realtime/RealtimeSessionService.hpp"
#include "Stream/StreamController.hpp"
#include "Service/Realtime/RealtimeEventRelayService.hpp"
#include <optional>

namespace xmax::detail {

/**
 * Coordinates allocation, keepalive, and release of the server session and RTC connection.
 */
class XmaxRealtimeConnectionManager {

  public:

    /**
     * Retains the shared API service for session requests.
     *
     * @param[in] api The API service for the realtime model and environment.
     */
    explicit XmaxRealtimeConnectionManager(
        std::shared_ptr<ApiService> api
    );

    /**
     * Validates RTC capability before allocating a server session to avoid creating billable resources
     * without a backend.
     *
     * @param[in,out] stream The current realtime transport controller.
     * @param[in] cancel The cooperative cancellation predicate.
     *
     * @return The validated server session owned by this controller.
     */
    const RealtimeSession& create(
        StreamController& stream,
        const Cancel& cancel
    );

    /**
     * Returns the current session; its identifier is empty before allocation.
     */
    const RealtimeSession& active() const {
        return service_.active();
    }

    /**
     * Joins the RTC room assigned by the server session.
     *
     * @param[in,out] stream The current realtime transport controller.
     * @param[in] format The baseline media encoding format.
     * @param[in] publish_audio Whether to publish a custom PCM track on join.
     * @param[in] callbacks The RTC frame and lifecycle callbacks.
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] canvas The model's generation canvas.
     */
    void join(
        StreamController& stream,
        xmax_video_format format,
        bool publish_audio,
        RtcCallbacks callbacks,
        const Cancel& cancel,
        xmax_video_format canvas
    );

        /**
     * Starts session billing heartbeats once after this session's generating state is committed.
     */
    void start_heartbeat(
        const std::string& session_id,
        std::chrono::milliseconds interval,
        std::function<void(Error)> failure
    );

    /**
     * Submits server closure concurrently with local RTC cleanup and drains session heartbeats.
     *
     * @param[in,out] stream The current realtime transport controller.
     */
    void close(
        StreamController& stream
    );

  private:

    /**
     * The server-session resource for the current model.
     */
    RealtimeSessionService service_;

    /**
     * The authenticated independent command-relay service.
     */
    RealtimeEventRelayService relay_;

    /**
     * The session-creation start used for the first billing grace duration.
     */
    std::optional<Clock::time_point> started_at_;

    /**
     * Whether this session has already started its HTTP heartbeat.
     */
    bool heartbeat_started_ = false;
};
} // namespace xmax::detail
