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
 * 从服务连接响应中读取必需的非空字符串字段。
 *
 * @param[in] j 已解析的服务端连接对象。
 * @param[in] key 必须存在且为非空字符串的字段名称。
 */
static std::string field(
    const json& j,
    const char* key
) {
    require(
        j.contains(key) && j[key].is_string() && !j[key].get_ref<const std::string&>().empty(),
        XMAX_SESSION_ERROR, "Session response is missing RTC connection fields"
    );
    return j[key].get<std::string>();
}

/**
 * 校验服务端会话响应，提取会话标识与 RTC 连接信息。
 *
 * @param[in] payload 服务端会话响应的 data 对象，必须包含会话及 RTC 必需字段。
 * @return 包含服务端会话标识和有效 RTC 信息的会话记录。
 */
RealtimeSession parse_connection(
    const json& payload
) {
    RealtimeSession c;
    c.id = field(payload, "sessionUid");
    require(payload.contains("modelExtra"), XMAX_SESSION_ERROR, "Session response has no modelExtra");

    json extra = payload["modelExtra"];

    if (extra.is_string()) {
        try {
            extra = json::parse(extra.get<std::string>());
        } catch (const json::exception&) {
            throw Error(XMAX_SESSION_ERROR, "Invalid RTC connection JSON");
        }
    }

    require(extra.is_object(), XMAX_SESSION_ERROR, "Invalid RTC connection payload");
    c.room.app_id = field(extra, "rtc_app_id");
    c.room.room = field(extra, "room_id");
    c.room.user = field(extra, "rtc_user_id");
    c.room.token = field(extra, "room_token");
    c.room.bot = field(extra, "rtc_bot_id");

    return c;
}

/**
 * 分配服务端会话并验证 RTC 信息，保留失败回滚所需的会话标识。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @return 已创建并验证连接信息的会话借用引用。
 */
const RealtimeSession& RealtimeSessionService::create(
    const Cancel& cancel
) {
    require(session_.id.empty(), XMAX_INVALID_STATE, "A server session is already allocated");

    const json body{{"model", api_->config().model}};
    const auto payload = api_->request("POST", "/session", &body, cancel, true);

    // 先保存已分配的会话标识，后续校验失败时仍可请求释放服务端资源。
    if (payload.contains("sessionUid") && payload["sessionUid"].is_string())
        session_.id = payload["sessionUid"].get<std::string>();

    session_ = parse_connection(payload);

    return session_;
}

/**
 * 刷新活动服务端会话，支持协作式取消。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 */
void RealtimeSessionService::heartbeat(
    const Cancel& cancel
) {
    require(!session_.id.empty(), XMAX_INVALID_STATE, "No server session is allocated");
    const auto value = api_->request("PUT", "/session/" + session_.id + "/heartbeat", nullptr, cancel, true);
    if (value.contains("status") && value["status"].is_string())
        require(value["status"] == "ACTIVE", XMAX_SESSION_ERROR, "Server session is no longer active");
}

/**
 * 关闭活动服务端会话并清除本地会话记录。
 */
void RealtimeSessionService::close() {
    // 即使服务端关闭失败，也清除本地会话所有权，避免复用无效会话。
    auto previous = std::exchange(session_, {});
    if (!previous.id.empty())
        api_->request("DELETE", "/session/" + previous.id, nullptr, [] { return false; }, true);
}
} // namespace xmax::detail
