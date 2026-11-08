//
//  RoomController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomController.hpp"
#include "Stream/Room/RoomEvent.hpp"

namespace xmax::detail {

RoomController::RoomController(
    RtcFactory factory,
    std::chrono::milliseconds heartbeat_interval
)
    : factory_(std::move(factory)), heartbeat_interval_(heartbeat_interval) {
}

RoomController::~RoomController() {
    leave();
}

void RoomController::allocate() {
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

void RoomController::join(
    const Connection& value,
    VideoEncodingConfiguration encoding,
    bool publish_audio,
    RtcCallbacks callbacks,
    const Cancel& cancel
) {
    require(
        rtc_ && !joined_,
        XMAX_INVALID_STATE,
        "Allocate an RTC backend before joining"
    );

    connection = value;
    commands_ = std::make_shared<RoomCommandController>(
        [this, weak = std::weak_ptr<Rtc>(rtc_)](const json& message, Cancel cancelled) {
            auto rtc = weak.lock();
            require(
                bool(rtc),
                XMAX_CANCELLED,
                "RTC room was released"
            );
            return sender_.enqueue(
                std::move(rtc),
                message,
                std::move(cancelled)
            );
        },
        relay_,
        value.user,
        value.bot
    );
    callbacks.message = [commands = commands_,
                         listener = std::move(callbacks.message)](auto sender, auto data) {
        commands->receive(sender, data);
        if (listener)
            listener(
                std::move(sender),
                std::move(data)
            );
    };

    try {
        rtc_->join(
            value,
            encoding,
            publish_audio,
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
        heartbeat_.start(sender_, rtc_, room_event("heartbeat", value), heartbeat_interval_);
    } catch (...) {
        leave();
        throw;
    }
}

void RoomController::leave() noexcept {
    heartbeat_.stop();
    std::atomic_store(
        &media_rtc_,
        std::shared_ptr<Rtc>{}
    );
    if (commands_)
        commands_->close();
    sender_.reset();
    if (rtc_) {
        rtc_->leave();
        rtc_.reset();
    }
    joined_ = false;
    connection = {};
    task.clear();
    commands_.reset();
    relay_ = {};
}

void RoomController::send(
    const json& message,
    const Cancel& cancel,
    const std::function<void()>& maintain,
    Clock::time_point deadline
) {
    require(
        joined_,
        XMAX_INVALID_STATE,
        "No RTC room is connected"
    );
    require(Clock::now() < deadline, XMAX_TIMEOUT, "Realtime generation start timed out");

    const auto event = message.value(
        "event",
        std::string()
    );
    const bool reliable =
        event == "start" || event == "change_condition" || event == "change_target_size";
    std::string xid;
    std::shared_future<void> completion;
    if (reliable) {
        auto ticket = commands_->submit(
            message,
            task,
            cancel
        );
        xid = std::move(ticket.xid);
        completion = std::move(ticket.completion);
    } else {
        completion = sender_.enqueue(
            rtc_,
            message,
            cancel
        );
    }

    try {
        while (completion.wait_until(std::min(deadline, Clock::now() + std::chrono::milliseconds(5))) !=
               std::future_status::ready) {
            require(
                !cancel(),
                XMAX_CANCELLED,
                "RTC message cancelled"
            );
            require(Clock::now() < deadline, XMAX_TIMEOUT, "Realtime generation start timed out");
            if (maintain)
                maintain();
        }
        completion.get();
        require(
            !cancel(),
            XMAX_CANCELLED,
            "RTC message cancelled"
        );
    } catch (...) {
        if (!xid.empty())
            commands_->cancel_xid(xid);
        throw;
    }
}

void RoomController::cancel_commands() {
    if (commands_)
        commands_->cancel(task);
}

void RoomController::event_relay(
    RoomCommandController::Relay relay
) {
    relay_ = std::move(relay);
}

void RoomController::tracks(
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
} // namespace xmax::detail
