//
//  XmaxRealtimeFactory.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeFactory.hpp"
#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Foundation/Storage/PreferenceStore.hpp"

namespace xmax::detail {

/**
 * Creates a realtime facade with native media, RTC, and model-scoped quality history.
 *
 * @param[in] api The shared service configuration and HTTP transport.
 * @param[in] adaptive_quality Whether to restore and adapt directional quality levels.
 *
 * @return An independent realtime facade retaining the shared service dependencies.
 */
std::unique_ptr<XmaxRealtimeManager> XmaxRealtimeFactory::make(
    std::shared_ptr<ApiService> api,
    bool adaptive_quality
) {
    require(
        bool(api),
        XMAX_INVALID_ARGUMENT,
        "API service is required"
    );
    auto history = std::make_shared<QualityHistory>(
        api->config().model,
        make_preference_store()
    );
    auto manager = std::make_unique<XmaxRealtimeManager>(
        std::move(api),
        make_rtc,
        std::chrono::seconds(10),
        make_camera_capture,
        std::move(history)
    );
    manager->adaptive(adaptive_quality);
    return manager;
}
} // namespace xmax::detail
