//
//  RealtimeEventRelayService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Network/ApiService.hpp"

namespace xmax::detail {

/**
 * Submits a complete frozen generation command independently of RTC delivery.
 */
class RealtimeEventRelayService {

  public:

    /**
     * Retains the authenticated realtime API service.
     */
    explicit RealtimeEventRelayService(
        std::shared_ptr<ApiService> api
    )
        : api_(std::move(api)) {
    }

    /**
     * Accepts matching new or duplicate receipts without confirming generation readiness.
     */
    void relay(
        const std::string& session_id,
        const std::string& model_id,
        const json& message,
        const Cancel& cancel
    ) const;

  private:

    /**
     * The service holding the client's environment and credentials.
     */
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
