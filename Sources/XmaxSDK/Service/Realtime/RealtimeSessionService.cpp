//
//  RealtimeSessionService.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Realtime/RealtimeSessionService.hpp"

namespace xmax::detail {

/**
 * Reads a required nonempty string from the service connection response.
 *
 * @param[in] j The parsed service connection object.
 * @param[in] key The field name that must exist and contain a nonempty string.
 */
static std::string field(
    const json& j,
    const char* key
) {
    require(
        j.contains(key) && j[key].is_string() && !j[key].get_ref<const std::string&>().empty(),
        XMAX_SESSION_ERROR,
        "Session response is missing RTC connection fields"
    );
    return j[key].get<std::string>();
}

/**
 * Validates the server-session response and extracts session and RTC connection information.
 *
 * @param[in] payload The session response's data object, containing required session and RTC fields.
 *
 * @return A session record with its server identifier and validated RTC information.
 */
RealtimeSession parse_connection(
    const json& payload
) {
    RealtimeSession c;
    c.id = field(
        payload,
        "sessionUid"
    );
    require(
        payload.contains("modelExtra"),
        XMAX_SESSION_ERROR,
        "Session response has no modelExtra"
    );

    json extra = payload["modelExtra"];

    if (extra.is_string()) {
        try {
            extra = json::parse(extra.get<std::string>());
        } catch (const json::exception&) {
            throw Error(
                XMAX_SESSION_ERROR,
                "Invalid RTC connection JSON"
            );
        }
    }

    require(
        extra.is_object(),
        XMAX_SESSION_ERROR,
        "Invalid RTC connection payload"
    );
    c.room.app_id = field(
        extra,
        "rtc_app_id"
    );
    c.room.room = field(
        extra,
        "room_id"
    );
    c.room.user = field(
        extra,
        "rtc_user_id"
    );
    c.room.token = field(
        extra,
        "room_token"
    );
    c.room.bot = field(
        extra,
        "rtc_bot_id"
    );

    return c;
}

/**
 * Allocates a server session and validates RTC information, retaining the identifier needed for
 * rollback.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 *
 * @return A borrowed reference to the session with validated connection information.
 */
const RealtimeSession& RealtimeSessionService::create(
    const Cancel& cancel
) {
    require(
        session_.id.empty(),
        XMAX_INVALID_STATE,
        "A server session is already allocated"
    );

    const json body{{"model", api_->config().model}};
    const auto payload = api_->request(
        "POST",
        "/session",
        &body,
        cancel,
        true
    );

    // Retain the allocated session identifier so later validation failures can release server resources.
    if (payload.contains("sessionUid") && payload["sessionUid"].is_string())
        session_.id = payload["sessionUid"].get<std::string>();

    session_ = parse_connection(payload);

    return session_;
}

/**
 * Refreshes the active server session with cooperative cancellation.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void RealtimeSessionService::heartbeat(
    const Cancel& cancel
) {
    require(
        !session_.id.empty(),
        XMAX_INVALID_STATE,
        "No server session is allocated"
    );

    const auto value = api_->request(
        "PUT",
        "/session/" + session_.id + "/heartbeat",
        nullptr,
        cancel,
        true
    );

    if (value.contains("status") && value["status"].is_string())
        require(
            value["status"] == "ACTIVE",
            XMAX_SESSION_ERROR,
            "Server session is no longer active"
        );
}

/**
 * Closes the active server session and clears its local record.
 */
void RealtimeSessionService::close() {
    // Clear local ownership even if server closing fails to avoid reusing an invalid session.
    auto previous = std::exchange(
        session_,
        {}
    );
    if (!previous.id.empty())
        api_->request(
            "DELETE",
            "/session/" + previous.id,
            nullptr,
            [] { return false; },
            true
        );
}
} // namespace xmax::detail
