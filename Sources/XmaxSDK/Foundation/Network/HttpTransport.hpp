//
//  HttpTransport.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * The HTTP request, response-size limit, progress and streaming callbacks, and timeout.
 */
struct HttpRequest {

    /**
     * The HTTP request method.
     */
    std::string method;

    /**
     * The absolute HTTPS URL, subject to the transport's URL and redirect policy.
     */
    std::string url;

    /**
     * The complete request-body bytes to send.
     */
    std::string body;

    /**
     * The ordered collection of HTTP header names and values.
     */
    std::vector<std::pair<std::string, std::string>> headers;

    /**
     * The HTTP request timeout, in milliseconds.
     */
    uint32_t timeout_ms = 15000;

    /**
     * The maximum response size, in bytes; exceeding it aborts transfer.
     */
    uint64_t response_limit = 4 * 1024 * 1024;

    /**
     * The progress callback receiving transferred bytes and then total bytes; unknown totals are 0.
     */
    std::function<void(uint64_t, uint64_t)> progress;

    /**
     * The streaming receiver taking a borrowed data pointer and byte count, valid only during the callback.
     */
    std::function<void(const char*, size_t)> receive;
};

/**
 * The HTTP status, headers, and received response body.
 */
struct HttpResponse {

    /**
     * The server's HTTP response status.
     */
    long status = 0;

    /**
     * Received body bytes; streaming reception need not retain the full response in memory.
     */
    std::string body;

    /**
     * The ordered collection of HTTP header names and values.
     */
    std::vector<std::pair<std::string, std::string>> headers{};
};

/**
 * A synchronous HTTP transport supporting cancellation, response limits, and streaming reception.
 */
class Http {

  public:

    /**
     * Releases the byte transport after all requests finish.
     *
     * @note Finish concurrent calls using this object before destroying it.
     */
    virtual ~Http() = default;

    /**
     * Performs a synchronous HTTP request with cancellation, limits, progress, and streaming reception.
     *
     * @param[in] request The request URL, timeout, response-size limit, and streaming receive callback.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     *
     * @return The HTTP status, headers, and nonstreamed body; transport failures throw an SDK exception.
     */
    virtual HttpResponse request(
        const HttpRequest& request,
        const Cancel& cancel
    ) = 0;
};

/**
 * Creates the platform's HTTPS byte transport.
 *
 * @return A shared HTTP transport for the current platform.
 */
std::shared_ptr<Http> make_http();
} // namespace xmax::detail
