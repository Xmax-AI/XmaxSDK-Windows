#pragma once
#include "Foundation/Common.hpp"
namespace xmax::detail {
// Byte transport only. Authentication, endpoints and API envelopes belong to Service.
struct HttpRequest {
    std::string method, url, body;
    std::vector<std::pair<std::string, std::string>> headers;
    uint32_t timeout_ms = 15000;
    uint64_t response_limit = 4 * 1024 * 1024;
    std::function<void(uint64_t, uint64_t)> progress;
    std::function<void(const char*, size_t)> receive;
};
struct HttpResponse {
    long status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers{};
};
class Http {
  public:
    virtual ~Http() = default;
    virtual HttpResponse request(const HttpRequest&, const Cancel&) = 0;
};
std::shared_ptr<Http> make_http();
} // namespace xmax::detail
