//
//  RealtimeSessionHeartbeat.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Realtime/RealtimeSessionHeartbeat.hpp"
#include "Foundation/Text/Utf8.hpp"

namespace xmax::detail {

RealtimeSessionHeartbeat::RealtimeSessionHeartbeat(
    std::shared_ptr<ApiService> api,
    std::string session_id,
    int64_t first_grace_seconds,
    std::chrono::milliseconds interval,
    std::function<void(Error)> failure
)
    : api_(std::move(api)),
      session_id_(std::move(session_id)),
      first_grace_seconds_(first_grace_seconds),
      interval_(interval),
      failure_(std::move(failure)),
      worker_([this] { run(); }) {
}

RealtimeSessionHeartbeat::~RealtimeSessionHeartbeat() {
    cancel();
    worker_.join();
}

void RealtimeSessionHeartbeat::cancel() {
    cancelled_ = true;
    wake_.notify_all();
}

void RealtimeSessionHeartbeat::run() {
    const Cancel cancelled = [this] { return cancelled_.load(); };
    std::optional<int64_t> grace = first_grace_seconds_;
    while (!cancelled()) {
        try {
            request(
                *api_,
                session_id_,
                cancelled,
                grace
            );
        } catch (const Error& error) {
            report_failure(error);
            return;
        } catch (const std::exception& error) {
            report_failure(Error(
                XMAX_API_ERROR,
                error.what()
            ));
            return;
        } catch (...) {
            report_failure(Error(
                XMAX_API_ERROR,
                "Session heartbeat failed"
            ));
            return;
        }

        grace.reset();
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wake_.wait_for(
            lock,
            interval_,
            cancelled
        );
    }
}

void RealtimeSessionHeartbeat::report_failure(
    Error error
) {
    if (!cancelled_ && failure_)
        failure_(std::move(error));
}

void RealtimeSessionHeartbeat::request(
    const ApiService& api,
    const std::string& session_id,
    const Cancel& cancelled,
    std::optional<int64_t> grace
) {
    const json body = grace ? json{{"firstHeartbeatGraceSeconds", *grace}} : json();
    const auto value = api.request(
        "PUT",
        "/session/" + session_id + "/heartbeat",
        grace ? &body : nullptr,
        cancelled,
        true
    );
    require(
        !cancelled(),
        XMAX_CANCELLED,
        "Session heartbeat cancelled"
    );

    const auto text = [&](const char* key) -> std::string_view {
        const auto field = value.find(key);
        return field != value.end() && field->is_string()
            ? trim_utf8_whitespace(field->get_ref<const std::string&>()) : std::string_view{};
    };
    require(!text("sessionUid").empty(), XMAX_SESSION_ERROR, "Invalid session response");

    const auto status = text("status");
    if (status.empty() || status == "ACTIVE")
        return;
    const auto reason = text("closeReason");
    throw Error(
        XMAX_SESSION_ERROR,
        reason.empty() ? "Session is no longer active: " + std::string(status) : std::string(reason)
    );
}
} // namespace xmax::detail
