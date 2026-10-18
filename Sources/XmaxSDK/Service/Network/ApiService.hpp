#pragma once
#include "Foundation/Json.hpp"
#include "Foundation/Network/HttpTransport.hpp"
namespace xmax::detail {
struct Config {
    std::string key, model = "x2.1.1-preview-cn";
    uint32_t timeout_ms = 15000;
    std::string base_url() const;
};
class ApiService {
  public:
    explicit ApiService(Config, std::shared_ptr<Http> = make_http());
    json request(const std::string& method, const std::string& path, const json* body, const Cancel&) const;
    const Config& config() const { return config_; }
    std::shared_ptr<Http> transport() const { return http_; }

  private:
    Config config_;
    std::shared_ptr<Http> http_;
};
} // namespace xmax::detail
