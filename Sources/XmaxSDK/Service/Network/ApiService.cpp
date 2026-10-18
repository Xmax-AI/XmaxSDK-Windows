#include "Service/Network/ApiService.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
namespace xmax::detail {
std::string Config::base_url() const {
    require(model == "x2.1.1-preview-cn" || model == "x2.1.1-preview-us", XMAX_INVALID_ARGUMENT,
            "Unsupported realtime model");
    return model == "x2.1.1-preview-cn" ? "https://cloud.xmax.22duck.cn/open/api/v1"
                                        : "https://api.xmax.ai/open/api/v1";
}
static json decode(const std::string& text, long status) {
    require(status >= 200 && status < 300, XMAX_API_ERROR,
            "Xmax API returned HTTP " + std::to_string(status));
    json response;
    try {
        response = json::parse(text);
    } catch (...) {
        throw Error(XMAX_API_ERROR, "Xmax API returned invalid JSON");
    }
    require(response.is_object() && response.contains("success") && response["success"].is_boolean() &&
                response["success"].get<bool>(),
            XMAX_API_ERROR, "Xmax API rejected the request");
    require(response.contains("data"), XMAX_API_ERROR, "Xmax API response has no data field");
    return response["data"];
}
ApiService::ApiService(Config config, std::shared_ptr<Http> http)
    : config_(std::move(config)), http_(std::move(http)) {
    (void)config_.base_url();
    require(config_.timeout_ms >= 100 && config_.timeout_ms <= 120000, XMAX_INVALID_ARGUMENT,
            "HTTP timeout must be between 100 and 120000 ms");
    require(bool(http_), XMAX_INVALID_ARGUMENT, "HTTP transport is required");
}
json ApiService::request(const std::string& method, const std::string& path, const json* body,
                         const Cancel& cancelled) const {
    const auto& config = config_;
    require(!config.key.empty(), XMAX_INVALID_ARGUMENT, "An Xmax API Key is required");
    require(config.key.find_first_of("\r\n") == std::string::npos, XMAX_INVALID_ARGUMENT, "Invalid API Key");
    require((method == "GET" || method == "POST" || method == "PUT" || method == "DELETE") && !path.empty() &&
                path[0] == '/' && path.find("://") == std::string::npos && path.find("//") != 0 &&
                path.find("..") == std::string::npos && path.find_first_of("\r\n\\#") == std::string::npos,
            XMAX_INVALID_ARGUMENT, "Only relative API paths and supported HTTP methods are accepted");
    require(!cancelled(), XMAX_CANCELLED, "Operation cancelled");
    const auto runtime = runtime_info();
    HttpRequest request;
    request.url = config.base_url() + path;
    request.method = method;
    request.timeout_ms = config.timeout_ms;
    request.headers = {{"Accept", "application/json"},
                       {"Content-Type", "application/json"},
                       {"X-Api-Key", config.key},
                       {"X-Platform", "windows"},
                       {"X-SDK-Version", runtime["sdk_version"].get<std::string>()},
                       {"X-OS-Version", runtime["os_version"].get<std::string>()},
                       {"X-Device-Model", runtime["device_model"].get<std::string>()}};
    if (body)
        request.body = body->dump();
    const auto response = http_->request(request, cancelled);
    return decode(response.body, response.status);
}
} // namespace xmax::detail
