#pragma once
#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Core/Storage/XmaxStorageManager.hpp"
#include "Foundation/Render/WindowRenderer.hpp"
namespace xmax::detail {
class XmaxClient {
  public:
    explicit XmaxClient(Config config, std::shared_ptr<Http> http = make_http())
        : api_(std::make_shared<ApiService>(std::move(config), std::move(http))) {}
    json request(const std::string& method, const std::string& path, const json* body,
                 const Cancel& cancel) const {
        return api_->request(method, path, body, cancel);
    }
    std::unique_ptr<XmaxRealtimeManager> create_realtime() const {
        auto history = std::make_shared<QualityHistory>(api_->config().model, make_preference_store());
        return std::make_unique<XmaxRealtimeManager>(api_, make_rtc, std::chrono::seconds(10),
                                                     make_camera_capture, std::move(history));
    }
    bool has_rtc() const { return rtc_available(); }
    uint64_t capabilities() const {
        return (rtc_available() ? XMAX_CAP_RTC : 0) | (camera_available() ? XMAX_CAP_CAMERA : 0) |
               (native_window_available() ? XMAX_CAP_NATIVE_WINDOW : 0) | XMAX_CAP_STORAGE;
    }
    std::vector<CameraDevice> cameras() const { return make_camera_capture()->devices(); }
    std::unique_ptr<XmaxStorageManager> create_storage() const {
        return std::make_unique<XmaxStorageManager>(api_);
    }

  private:
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
