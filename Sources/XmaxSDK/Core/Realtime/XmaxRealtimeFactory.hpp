//
//  XmaxRealtimeFactory.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/XmaxRealtimeLifecycleManager.hpp"
#include "Service/Network/ApiService.hpp"

namespace xmax::detail {

/**
 * The realtime facade created with platform adapters and shared lifecycle dependencies.
 */
class XmaxRealtimeManager;

/**
 * Assembles shared lifecycle state, scoped cleanup, and runtime failure routing.
 */
class XmaxRealtimeFactory {

  public:

    /**
     * Creates a realtime facade with native media, RTC, and model-scoped quality history.
     *
     * @param[in] api The shared service configuration and HTTP transport.
     * @param[in] adaptive_quality Whether to restore and adapt directional quality levels.
     *
     * @return An independent realtime facade retaining the shared service dependencies.
     */
    static std::unique_ptr<XmaxRealtimeManager> make(
        std::shared_ptr<ApiService> api,
        bool adaptive_quality
    );

    /**
     * Connects component callbacks before capture or RTC operations begin.
     *
     * @param[in,out] lifecycle The lifecycle owner, drained before its resource controllers are destroyed.
     * @param[in,out] errors The failure manager shared by media and connection producers.
     * @param[in] cleanup The handler releasing resources on the lifecycle worker.
     * @param[in] state_handler The resource bookkeeping performed after state commitment.
     */
    static void assemble(
        XmaxRealtimeLifecycleManager& lifecycle,
        XmaxRealtimeErrorManager& errors,
        XmaxRealtimeLifecycleManager::CleanupHandler cleanup,
        XmaxRealtimeLifecycleManager::StateHandler state_handler
    ) {
        lifecycle.configure(
            errors,
            std::move(cleanup),
            std::move(state_handler)
        );
        errors.failure_handler([&lifecycle](Error error, RealtimeTerminationScope scope, Cancel current) {
            lifecycle.terminate_failure(
                std::move(error),
                scope,
                std::move(current)
            );
        });
    }
};
} // namespace xmax::detail
