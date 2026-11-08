//
//  XmaxRealtimeConnectionManager.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeConnectionManager.hpp"
#include <cmath>
#include <future>

namespace xmax::detail {

XmaxRealtimeConnectionManager::XmaxRealtimeConnectionManager(
    std::shared_ptr<ApiService> api
)
    : service_(api), relay_(std::move(api)) {
}

const RealtimeSession& XmaxRealtimeConnectionManager::create(
    StreamController& stream,
    const Cancel& cancel
) {
    stream.allocate();
    started_at_ = Clock::now();
    heartbeat_started_ = false;
    return service_.create(cancel);
}

void XmaxRealtimeConnectionManager::join(
    StreamController& stream,
    xmax_video_format format,
    bool publish_audio,
    RtcCallbacks callbacks,
    const Cancel& cancel,
    xmax_video_format canvas
) {
    stream.event_relay(
        [relay = relay_, session = service_.active()](const json& message, const Cancel& cancelled) {
            relay.relay(
                session.id,
                session.model_id,
                message,
                cancelled
            );
        }
    );
    stream.join(
        service_.active().room,
        format,
        publish_audio,
        std::move(callbacks),
        cancel,
        canvas
    );
}

void XmaxRealtimeConnectionManager::start_heartbeat(
    const std::string& session_id,
    std::chrono::milliseconds interval,
    std::function<void(Error)> failure
) {
    if (heartbeat_started_ || !started_at_ || session_id.empty() || session_id != service_.active().id)
        return;

    const auto seconds = std::chrono::duration<double>(Clock::now() - *started_at_).count();
    const auto grace = static_cast<int64_t>(std::ceil(std::max(0.0, seconds)));
    service_.start_heartbeat(
        grace,
        interval,
        std::move(failure)
    );
    heartbeat_started_ = true;
}

void XmaxRealtimeConnectionManager::close(
    StreamController& stream
) {
    started_at_.reset();
    heartbeat_started_ = false;
    auto closing = std::async(
        std::launch::async,
        [this] { service_.close(); }
    );
    stream.stop();
    closing.get();
}
} // namespace xmax::detail
