//
//  RealtimeEventRelayService.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Realtime/RealtimeEventRelayService.hpp"

namespace xmax::detail {

void RealtimeEventRelayService::relay(
    const std::string& session_id,
    const std::string& model_id,
    const json& message,
    const Cancel& cancel
) const {
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Event relay cancelled"
    );
    if (model_id.empty()) {
        XmaxLogger::write(
            LogCategory::Room,
            XMAX_LOG_WARNING,
            [](auto) { return "HTTP signaling relay skipped: session has no model instance ID"; }
        );
        return;
    }

    const json body{{"sessionId", session_id}, {"modelId", model_id}, {"data", message}};
    const auto receipt = api_->request(
        "POST",
        "/client-event/relay",
        &body,
        cancel,
        true
    );
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Event relay cancelled"
    );
    require(
        receipt.is_object() && receipt.contains("xid") && receipt["xid"].is_string() &&
            receipt["xid"] == message.at("xid") && receipt.contains("accepted") &&
            receipt["accepted"].is_boolean(),
        XMAX_API_ERROR,
        "Server returned invalid event relay response"
    );

    XmaxLogger::write(
        LogCategory::Room,
        XMAX_LOG_INFO,
        [&](auto) {
            return "HTTP signaling relay: event=" + message.at("event").get<std::string>() +
                   ", xid=" + message.at("xid").get<std::string>() + ", accepted=" + receipt["accepted"].dump();
        }
    );
}
} // namespace xmax::detail
