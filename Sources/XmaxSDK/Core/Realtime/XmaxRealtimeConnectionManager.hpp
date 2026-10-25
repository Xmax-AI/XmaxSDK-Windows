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
    )
        : service_(std::move(api)) {
    }

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
    ) {
        stream.allocate();
        return service_.create(cancel);
    }

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
     * @param[in] microphone Whether to enable the microphone.
     * @param[in] callbacks The RTC frame and lifecycle callbacks.
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] canvas The model's generation canvas.
     */
    void join(
        StreamController& stream,
        xmax_video_format format,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel,
        xmax_video_format canvas
    ) {
        stream.join(
            service_.active().room,
            format,
            microphone,
            std::move(callbacks),
            cancel,
            canvas
        );
    }

    /**
     * Keeps both the server session and RTC room alive.
     *
     * @param[in,out] stream The current realtime transport controller.
     * @param[in] cancel The cooperative cancellation predicate.
     */
    void heartbeat(
        StreamController& stream,
        const Cancel& cancel
    ) {
        service_.heartbeat(cancel);
        stream.heartbeat(cancel);
    }

    /**
     * Stops RTC publication and drains callbacks before releasing the server session.
     *
     * @param[in,out] stream The current realtime transport controller.
     */
    void close(
        StreamController& stream
    ) {
        stream.stop();
        service_.close();
    }

  private:

    /**
     * The server-session resource for the current model.
     */
    RealtimeSessionService service_;
};
} // namespace xmax::detail
