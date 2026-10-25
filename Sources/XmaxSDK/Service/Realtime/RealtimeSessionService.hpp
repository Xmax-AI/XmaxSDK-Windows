//
//  RealtimeSessionService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Realtime/RealtimeSession.hpp"
#include "Foundation/RTC/RtcPlatform.hpp"
#include "Service/Network/ApiService.hpp"

namespace xmax::detail {

/**
 * Validates the server-session response and extracts session and RTC connection information.
 *
 * @param[in] payload The session response's data object, containing required session and RTC fields.
 *
 * @return A session record with its server identifier and validated RTC information.
 */
RealtimeSession parse_connection(
    const json& payload
);

/**
 * Manages session creation, keepalive, and closing, retaining cleanup identifiers after partial failure.
 */
class RealtimeSessionService {

  public:

    /**
     * Retains the shared API service for session creation, keepalive, and closing.
     */
    explicit RealtimeSessionService(
        std::shared_ptr<ApiService> api
    )
        : api_(std::move(api)) {
    }

    /**
     * Allocates a server session and validates RTC information, retaining the identifier needed for
     * rollback.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     *
     * @return A borrowed reference to the session with validated connection information.
     */
    const RealtimeSession& create(
        const Cancel& cancel
    );

    /**
     * Refreshes the active server session with cooperative cancellation.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void heartbeat(
        const Cancel& cancel
    );

    /**
     * Closes the active server session and clears its local record.
     */
    void close();

    /**
     * Borrows the current session record on the lifecycle executor.
     *
     * @return A borrowed active-session reference; do not rely on its previous content after mutation or
     * closing.
     */
    const RealtimeSession& active() const {
        return session_;
    }

  private:

    /**
     * Retains the shared API service so child controllers survive destruction of the client handle.
     */
    std::shared_ptr<ApiService> api_;

    /**
     * The active session and RTC information, cleared on closing.
     */
    RealtimeSession session_;
};
} // namespace xmax::detail
