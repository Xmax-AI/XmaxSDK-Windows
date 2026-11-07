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
#include "Foundation/Text/Utf8.hpp"

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
    auto value = j.contains(key) && j[key].is_string()
                     ? trim_utf8_whitespace(j[key].get_ref<const std::string&>())
                     : std::string_view{};
    require(
        !value.empty(),
        XMAX_SESSION_ERROR,
        "Session response is missing RTC connection fields"
    );
    return std::string(value);
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
    if (payload.contains("modelUid") && payload["modelUid"].is_string())
        c.model_id = trim_utf8_whitespace(payload["modelUid"].get_ref<const std::string&>());
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
    require(c.room.app_id == realtime_rtc_app_id, XMAX_SESSION_ERROR, "Session RTC app does not match the SDK configuration");
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
        session_.id = trim_utf8_whitespace(payload["sessionUid"].get_ref<const std::string&>());

    session_ = parse_connection(payload);

    return session_;
}

/**
 * Refreshes the active server session with cooperative cancellation.
 *
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void RealtimeSessionService::heartbeat(
    const Cancel& cancel,
    std::optional<int64_t> first_grace_seconds
) {
    require(
        !session_.id.empty(),
        XMAX_INVALID_STATE,
        "No server session is allocated"
    );
    RealtimeSessionHeartbeat::request(
        *api_,
        session_.id,
        cancel,
        first_grace_seconds
    );
}

RealtimeSessionService::~RealtimeSessionService() {
    stop_heartbeat();
}

void RealtimeSessionService::start_heartbeat(
    int64_t first_grace_seconds,
    std::chrono::milliseconds interval,
    std::function<void(Error)> failure
) {
    require(
        !session_.id.empty() && first_grace_seconds >= 0 && interval.count() > 0,
        XMAX_INVALID_STATE,
        "Invalid session heartbeat configuration"
    );
    if (heartbeat_)
        return;
    heartbeat_ = std::make_unique<RealtimeSessionHeartbeat>(
        api_,
        session_.id,
        first_grace_seconds,
        interval,
        std::move(failure)
    );
}

void RealtimeSessionService::stop_heartbeat() {
    heartbeat_.reset();
}

/**
 * Closes the active server session and clears its local record.
 */
void RealtimeSessionService::close() {
    auto previous = std::exchange(session_, {});
    if (heartbeat_)
        heartbeat_->cancel();
    // Submit server closure before draining an active HTTP phase or local RTC cleanup.
    try {
        if (!previous.id.empty())
            api_->request(
                "DELETE",
                "/session/" + previous.id,
                nullptr,
                [] { return false; },
                true
            );
    } catch (...) {
        stop_heartbeat();
        throw;
    }
    stop_heartbeat();
}
} // namespace xmax::detail
