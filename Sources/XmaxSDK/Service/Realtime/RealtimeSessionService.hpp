#pragma once
#include "Foundation/RTC/RtcPlatform.hpp"
#include "Service/Network/ApiService.hpp"
namespace xmax::detail {
struct RealtimeSession {
    std::string id;
    Connection room;
};
RealtimeSession parse_connection(const json&);
// Called from the Core lifecycle executor. Owns the server allocation, including rollback IDs.
class RealtimeSessionService {
  public:
    explicit RealtimeSessionService(std::shared_ptr<ApiService> api) : api_(std::move(api)) {}
    const RealtimeSession& create(const Cancel&);
    void heartbeat(const Cancel&);
    void close();
    const RealtimeSession& active() const { return session_; }

  private:
    std::shared_ptr<ApiService> api_;
    RealtimeSession session_;
};
} // namespace xmax::detail
