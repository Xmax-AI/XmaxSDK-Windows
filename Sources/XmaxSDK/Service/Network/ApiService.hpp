//
//  ApiService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Network/Config.hpp"
#include "Foundation/Json.hpp"
#include "Foundation/Models/RealtimeModelDefinition.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Network/HttpTransport.hpp"

namespace xmax::detail {

/**
 * Executes authenticated API requests for the configured service environment and consistently parses
 * responses and errors.
 */
class ApiService {

  public:

    /**
     * Validates and retains API configuration, HTTP transport, and credential-redaction protection.
     *
     * @param[in] config The copied API credentials, model, and request timeout.
     * @param[in] http The shared HTTP byte transport retained by this service.
     */
    explicit ApiService(
        Config config,
        std::shared_ptr<Http> http = make_http()
    );

    /**
     * Authenticates a relative API request, validates the response envelope, and returns its data payload.
     *
     * @param[in] method The HTTP request method.
     * @param[in] path The resource path relative to the current model's API root.
     * @param[in] body An optional JSON request body; a null pointer omits it.
     * @param[in] cancelled A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] realtime Whether to use the realtime-session API endpoint.
     *
     * @return The data payload of a successfully validated service response.
     */
    json request(
        const std::string& method,
        const std::string& path,
        const json* body,
        const Cancel& cancelled,
        bool realtime = false
    ) const;

    /**
     * Returns the read-only configuration copied when this service was created.
     *
     * @return The service-owned configuration reference, valid until service destruction.
     */
    const Config& config() const {
        return config_;
    }

    /**
     * Shares the byte transport for storage requests without JSON response envelopes.
     *
     * @return The shared underlying HTTP byte transport.
     */
    std::shared_ptr<Http> transport() const {
        return http_;
    }

  private:

    /**
     * The API configuration copied at service creation.
     */
    Config config_;

    /**
     * The shared HTTPS byte transport.
     */
    std::shared_ptr<Http> http_;

    /**
     * The guard retaining API credential registration for log redaction.
     */
    std::shared_ptr<const std::string> log_key_;
};
} // namespace xmax::detail
