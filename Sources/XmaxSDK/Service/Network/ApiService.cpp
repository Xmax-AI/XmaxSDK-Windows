//
//  ApiService.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Network/ApiService.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Service/Network/ApiLogger.hpp"

namespace xmax::detail {

/**
 * 校验 HTTP 与服务响应封装，返回已验证的数据负载。
 *
 * @param[in] text 服务端返回的 UTF-8 JSON 响应封装，必须包含 success 和 data 字段。
 * @param[in] status 服务端返回的 HTTP 状态码，仅接受 [200, 299]。
 */
static json decode(
    const std::string& text,
    long status
) {
    json response;
    try {
        response = json::parse(text);
    } catch (...) {
        throw Error(XMAX_API_ERROR, "Xmax API returned invalid JSON", 0, int32_t(status));
    }

    int32_t api_code = 0;
    if (response.is_object() && response.contains("code") && response["code"].is_number_integer()) {
        const auto value = response["code"].get<int64_t>();
        if (value >= INT32_MIN && value <= INT32_MAX)
            api_code = int32_t(value);
    }
    if (status < 200 || status >= 300 || !response.is_object() || !response.contains("success") ||
        !response["success"].is_boolean() || !response["success"].get<bool>()) {
        std::string message = "Xmax API rejected the request";
        if (response.is_object() && response.contains("message") && response["message"].is_string())
            message = XmaxLogger::redact(response["message"].get<std::string>());
        throw Error(XMAX_API_ERROR, std::move(message), api_code, int32_t(status));
    }

    if (!response.contains("data"))
        throw Error(XMAX_API_ERROR, "Xmax API response has no data field", api_code, int32_t(status));

    return response["data"];
}

/**
 * 校验并保存 API 配置、HTTP 传输器及凭证脱敏保护对象。
 *
 * @param[in] config 复制后的 API 凭证、模型与请求超时配置。
 * @param[in] http 由当前服务保留的共享 HTTP 字节传输器。
 */
ApiService::ApiService(
    Config config,
    std::shared_ptr<Http> http
)
    : config_(std::move(config)), http_(std::move(http)), log_key_(XmaxLogger::protect_secret(config_.key)) {
    (void)config_.base_url();
    require(
        config_.timeout_ms >= 100 && config_.timeout_ms <= 120000, XMAX_INVALID_ARGUMENT,
        "HTTP timeout must be between 100 and 120000 ms"
    );
    require(bool(http_), XMAX_INVALID_ARGUMENT, "HTTP transport is required");
}

/**
 * 对相对 API 请求进行鉴权，校验响应封装并返回数据负载。
 *
 * @param[in] method HTTP 请求方法。
 * @param[in] path 相对当前模型 API 根地址的资源路径。
 * @param[in] body 可选 JSON 请求体，空指针表示不发送请求体。
 * @param[in] cancelled 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] realtime 是否使用实时会话 API 地址。
 * @return 服务响应中已通过状态校验的 data 负载。
 */
json ApiService::request(
    const std::string& method,
    const std::string& path,
    const json* body,
    const Cancel& cancelled,
    bool realtime
) const {
    const auto& config = config_;
    require(!config.key.empty(), XMAX_INVALID_API_KEY, "An Xmax API Key is required");
    require(config.key.find_first_of("\r\n") == std::string::npos, XMAX_INVALID_ARGUMENT, "Invalid API Key");
    require(
        (method == "GET" || method == "POST" || method == "PUT" || method == "DELETE") && !path.empty() &&
            path[0] == '/' && path.find("://") == std::string::npos && path.find("//") != 0 &&
            path.find("..") == std::string::npos && path.find_first_of("\r\n\\#") == std::string::npos,
        XMAX_INVALID_ARGUMENT, "Only relative API paths and supported HTTP methods are accepted"
    );
    require(!cancelled(), XMAX_CANCELLED, "Operation cancelled");

    const auto runtime = runtime_info();
    HttpRequest request;
    request.url = config.base_url(realtime) + path;
    request.method = method;
    request.timeout_ms = config.timeout_ms;
    request.headers = {
        {"Accept", "application/json"},
        {"Content-Type", "application/json"},
        {"X-Api-Key", config.key},
        {"X-Platform", "windows"},
        {"X-SDK-Version", runtime["sdk_version"].get<std::string>()},
        {"X-OS-Version", runtime["os_version"].get<std::string>()},
        {"X-Device-Model", runtime["device_model"].get<std::string>()}
    };

    if (body)
        request.body = body->dump();

    const auto started = Clock::now();
    const auto duration = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    };
    HttpResponse response;
    bool received = false;

    try {
        response = http_->request(request, cancelled);
        received = true;

        auto result = decode(response.body, response.status);
        ApiLogger::response(method, path, response, duration(), true);

        return result;
    } catch (const std::exception& error) {
        if (received)
            ApiLogger::response(method, path, response, duration(), false);
        else
            ApiLogger::failure(method, path, error.what(), duration());
        throw;
    }
}
} // namespace xmax::detail
