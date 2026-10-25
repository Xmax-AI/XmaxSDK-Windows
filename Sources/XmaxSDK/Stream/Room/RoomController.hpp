//
//  RoomController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Room/RoomEvent.hpp"
#include "Stream/Room/RoomMessageSender.hpp"

namespace xmax::detail {

/**
 * Manages RTC room allocation, joining, leaving, cross-thread publication, and signaling.
 */
class RoomController {

  public:

    /**
     * Retains the RTC adapter factory without connecting before room join.
     *
     * @param[in] factory The native RTC backend factory.
     */
    explicit RoomController(
        RtcFactory factory = make_rtc
    )
        : factory_(std::move(factory)) {
    }

    /**
     * Drains room callbacks and releases the native backend.
     */
    ~RoomController() {
        leave();
    }

    /**
     * Validates and allocates a native RTC backend before creating a server session.
     */
    void allocate() {
        require(
            !rtc_,
            XMAX_INVALID_STATE,
            "An RTC backend is already allocated"
        );
        rtc_ = factory_();
        require(
            bool(rtc_),
            XMAX_UNSUPPORTED,
            "No RTC backend is available"
        );
    }

    /**
     * Joins the server-assigned room and enables media-thread publication only after success.
     *
     * @param[in] value The server-assigned room identity and credentials.
     * @param[in] encoding The uplink encoding configuration for this join.
     * @param[in] microphone Whether to start the microphone.
     * @param[in] callbacks The room's result-frame, statistics, and failure callbacks.
     * @param[in] cancel The cooperative cancellation predicate.
     */
    void join(
        const Connection& value,
        VideoEncodingConfiguration encoding,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel
    ) {
        require(
            rtc_ && !joined_,
            XMAX_INVALID_STATE,
            "Allocate an RTC backend before joining"
        );

        connection = value;
        try {
            rtc_->join(
                value,
                encoding,
                microphone,
                std::move(callbacks),
                cancel
            );
            require(
                !cancel(),
                XMAX_CANCELLED,
                "RTC join cancelled"
            );

            joined_ = true;
            std::atomic_store(
                &media_rtc_,
                rtc_
            );
        } catch (...) {
            leave();
            throw;
        }
    }

    /**
     * Disables media-thread access, leaves the room, and drains callbacks; safe to repeat.
     */
    void leave() noexcept {
        std::atomic_store(
            &media_rtc_,
            std::shared_ptr<Rtc>{}
        );
        if (rtc_) {
            rtc_->leave();
            rtc_.reset();
        }
        joined_ = false;
        connection = {};
        task.clear();
        sender_.reset();
    }

    /**
     * Reports whether room join has completed.
     */
    bool joined() const {
        return joined_;
    }

    /**
     * Borrows the native backend on the lifecycle thread only; invalid after leaving.
     */
    Rtc* backend() const {
        return rtc_.get();
    }

    /**
     * Publishes frames through an atomic room snapshot from a media thread.
     *
     * @param[in] frame The video frame, valid throughout the call.
     */
    void push(
        const Frame& frame
    ) {
        const auto rtc = std::atomic_load(&media_rtc_);
        if (rtc)
            rtc->push(frame);
    }

    /**
     * The room connection information, accessed only on the lifecycle thread.
     */
    Connection connection;

    /**
     * The generation task identifier, or empty when generation is inactive.
     */
    std::string task;

    /**
     * Sends room signaling using the shared send budget.
     *
     * @param[in] message The unfragmented business message.
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] maintain The keepalive operation invoked while waiting for send budget.
     */
    void send(
        const json& message,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    ) {
        require(
            joined_,
            XMAX_INVALID_STATE,
            "No RTC room is connected"
        );
        sender_.send(
            *rtc_,
            message,
            cancel,
            maintain
        );
    }

    /**
     * Sends normalized points for the current task, rejecting empty or invalid coordinates.
     *
     * @param[in] points The coordinates in sampling order, each in [0, 1].
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] maintain The keepalive callback invoked during send waits.
     */
    void tracks(
        const std::vector<xmax_point>& points,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        require(
            !task.empty(),
            XMAX_INVALID_STATE,
            "No generation task is active"
        );
        json values = json::array();
        for (const auto& point : points)
            values.push_back(json::array({point.x, point.y}));
        send(
            {{"event", "tracks"}, {"tracks", std::move(values)}, {"user_id", connection.user}, {"uid", task}},
            cancel,
            maintain
        );
    }

  private:

    /**
     * The native RTC factory; allocated backends are managed by this room.
     */
    RtcFactory factory_;

    /**
     * The native RTC backend owned by the lifecycle thread.
     */
    std::shared_ptr<Rtc> rtc_;

    /**
     * The joined-room backend read atomically by media threads.
     */
    std::shared_ptr<Rtc> media_rtc_;

    /**
     * Whether RTC join completed, accessed only by the lifecycle thread.
     */
    bool joined_ = false;

    /**
     * The sender managing fragmentation and sliding-window budget.
     */
    RoomMessageSender sender_;
};
} // namespace xmax::detail
