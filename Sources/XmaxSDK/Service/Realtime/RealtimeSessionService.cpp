#include "Service/Realtime/RealtimeSessionService.hpp"
namespace xmax::detail {
static std::string field(const json& j, const char* key) {
    require(j.contains(key) && j[key].is_string() && !j[key].get_ref<const std::string&>().empty(),
            XMAX_API_ERROR, "Session response is missing RTC connection fields");
    return j[key].get<std::string>();
}
RealtimeSession parse_connection(const json& payload) {
    RealtimeSession c;
    c.id = field(payload, "sessionUid");
    require(payload.contains("modelExtra"), XMAX_API_ERROR, "Session response has no modelExtra");
    json extra = payload["modelExtra"];
    if (extra.is_string()) {
        try {
            extra = json::parse(extra.get<std::string>());
        } catch (const json::exception&) {
            throw Error(XMAX_API_ERROR, "Invalid RTC connection JSON");
        }
    }
    require(extra.is_object(), XMAX_API_ERROR, "Invalid RTC connection payload");
    c.room.app_id = field(extra, "rtc_app_id");
    c.room.room = field(extra, "room_id");
    c.room.user = field(extra, "rtc_user_id");
    c.room.token = field(extra, "room_token");
    c.room.bot = field(extra, "rtc_bot_id");
    return c;
}
const RealtimeSession& RealtimeSessionService::create(const Cancel& cancel) {
    require(session_.id.empty(), XMAX_INVALID_STATE, "A server session is already allocated");
    const json body{{"model", api_->config().model}};
    const auto payload = api_->request("POST", "/session", &body, cancel);
    // Preserve the allocation ID before validating the rest of the response.
    if (payload.contains("sessionUid") && payload["sessionUid"].is_string())
        session_.id = payload["sessionUid"].get<std::string>();
    session_ = parse_connection(payload);
    return session_;
}
void RealtimeSessionService::heartbeat(const Cancel& cancel) {
    require(!session_.id.empty(), XMAX_INVALID_STATE, "No server session is allocated");
    const auto value = api_->request("PUT", "/session/" + session_.id + "/heartbeat", nullptr, cancel);
    if (value.contains("status") && value["status"].is_string())
        require(value["status"] == "ACTIVE", XMAX_API_ERROR, "Server session is no longer active");
}
void RealtimeSessionService::close() {
    // Local ownership must be cleared even if the server cannot confirm deletion.
    auto previous = std::exchange(session_, {});
    if (!previous.id.empty())
        api_->request("DELETE", "/session/" + previous.id, nullptr, [] { return false; });
}
} // namespace xmax::detail
