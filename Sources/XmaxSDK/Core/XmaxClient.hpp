//
//  XmaxClient.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Core/Storage/XmaxStorageManager.hpp"
#include "Foundation/Render/WindowRenderer.hpp"

namespace xmax::detail {

/**
 * Provides shared API configuration and independent realtime and storage controller factories.
 */
class XmaxClient {

  public:

    /**
     * Copies API configuration and retains the HTTP transport for child controllers.
     *
     * @param[in] config The copied API credentials, model identifier, and HTTP timeout.
     * @param[in] http The shared HTTP transport retained by services.
     */
    explicit XmaxClient(
        Config config,
        std::shared_ptr<Http> http = make_http()
    )
        : api_(std::make_shared<ApiService>(
            std::move(config),
            std::move(http)
        )) {
        XmaxLogger::write(
            LogCategory::Realtime,
            XMAX_LOG_INFO,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "创建 SDK Client (Client Created)",
                    "Client Created"
                ) +
                       "\n└─ model=" + api_->config().model;
            }
        );
    }

    /**
     * Performs authenticated API requests with cooperative cancellation.
     *
     * @param[in] method The HTTP request method.
     * @param[in] path The resource path relative to the current model's API root.
     * @param[in] body An optional JSON request body; a null pointer omits it.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     *
     * @return The data payload of a successfully validated service response.
     */
    json request(
        const std::string& method,
        const std::string& path,
        const json* body,
        const Cancel& cancel
    ) const {
        return api_->request(
            method,
            path,
            body,
            cancel
        );
    }

    /**
     * Creates a realtime controller with its own lifecycle and quality history.
     *
     * @param[in] model The realtime model identifier; empty retains the client's configured model.
     * @param[in] adaptive_quality Whether to use automatic quality adaptation and persisted history.
     *
     * @return An exclusively owned realtime controller whose configuration survives client destruction.
     */
    std::unique_ptr<XmaxRealtimeManager> create_realtime(
        const std::string& model = "",
        bool adaptive_quality = true
    ) const {
        auto api = api_;
        if (!model.empty() && model != api_->config().model) {
            auto config = api_->config();
            config.model = model;
            api = std::make_shared<ApiService>(
                std::move(config),
                api_->transport()
            );
        }

        auto history = std::make_shared<QualityHistory>(
            api->config().model,
            make_preference_store()
        );
        auto manager = std::make_unique<XmaxRealtimeManager>(
            api,
            make_rtc,
            std::chrono::seconds(10),
            make_camera_capture,
            std::move(history)
        );
        manager->adaptive(adaptive_quality);
        return manager;
    }

    /**
     * Reports whether this SDK build provides a native RTC backend.
     */
    bool has_rtc() const {
        return rtc_available();
    }

    /**
     * Returns the build's C ABI capability flags.
     *
     * @return The bitmask of XMAX_CAP_* capabilities.
     */
    uint64_t capabilities() const {
        return (rtc_available() ? XMAX_CAP_RTC | XMAX_CAP_REMOTE_MEDIA_OUTPUT : 0) |
               (camera_available() ? XMAX_CAP_CAMERA : 0) |
               (native_window_available() ? XMAX_CAP_NATIVE_WINDOW : 0) | XMAX_CAP_STORAGE |
               XMAX_CAP_TRAJECTORY;
    }

    /**
     * Enumerates platform cameras without starting capture.
     *
     * @return A camera information snapshot, or an empty collection if no cameras are available.
     */
    std::vector<CameraDevice> cameras() const {
        return make_camera_capture()->devices();
    }

    /**
     * Creates an independent storage controller retaining shared API services.
     *
     * @return An exclusively owned storage controller whose configuration survives client destruction.
     */
    std::unique_ptr<XmaxStorageManager> create_storage() const {
        return std::make_unique<XmaxStorageManager>(api_);
    }

  private:

    /**
     * Retains the shared API service so child controllers survive destruction of the client handle.
     */
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
