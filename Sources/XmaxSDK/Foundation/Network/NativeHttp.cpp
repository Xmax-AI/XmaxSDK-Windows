//
//  NativeHttp.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Network/HttpTransport.hpp"
#include <algorithm>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
namespace xmax::detail {
#ifdef _WIN32

/**
 * Captures a failed WinHTTP operation's system error and diagnostic immediately.
 *
 * @param[in] success Whether WinHTTP succeeded; failure reads GetLastError immediately.
 * @param[in] operation The diagnostic name of the WinHTTP operation.
 */
static void http_checked(
    BOOL success,
    const char* operation
) {
    if (success)
        return;

    const DWORD code = GetLastError(); // Capture before formatting or logging can change it.
    wchar_t buffer[1024]{};
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        buffer,
        DWORD(std::size(buffer)),
        nullptr
    );
    std::string detail;

    if (length) {
        const auto count = WideCharToMultiByte(
            CP_UTF8,
            0,
            buffer,
            int(length),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        detail.resize(size_t(count));
        WideCharToMultiByte(
            CP_UTF8,
            0,
            buffer,
            int(length),
            detail.data(),
            count,
            nullptr,
            nullptr
        );

        while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n'))
            detail.pop_back();
    }

    throw Error(
        XMAX_NETWORK_ERROR,
        std::string(operation) + " failed (WinHTTP " + std::to_string(code) + ")" +
            (detail.empty() ? "" : ": " + detail)
    );
}

/**
 * Exclusively owns a WinHTTP handle and closes it on destruction.
 */
struct InternetHandle {

    /**
     * The exclusively owned WinHTTP handle.
     */
    HINTERNET handle = nullptr;

    /**
     * Takes ownership of a valid WinHTTP handle; null reports initialization failure.
     *
     * @param[in] h The non-null WinHTTP handle whose ownership is transferred.
     */
    explicit InternetHandle(
        HINTERNET h
    )
        : handle(h) {
        http_checked(
            h != nullptr,
            "WinHTTP initialization"
        );
    }

    /**
     * Closes the owned WinHTTP handle.
     */
    ~InternetHandle() {
        if (handle)
            WinHttpCloseHandle(handle);
    }

    /**
     * Returns a borrowed WinHTTP handle without closing or transferring it.
     *
     * @return The borrowed handle, valid until this owner is destroyed.
     */
    operator HINTERNET() const {
        return handle;
    }
};

/**
 * Converts valid UTF-8 request text to Windows UTF-16.
 *
 * @param[in] s The valid UTF-8 text to convert.
 */
static std::wstring wide(
    const std::string& s
) {
    if (s.empty())
        return {};

    int n = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        s.data(),
        int(s.size()),
        nullptr,
        0
    );
    require(
        n > 0,
        XMAX_INVALID_ARGUMENT,
        "Invalid UTF-8 request value"
    );

    std::wstring out(
        size_t(n),
        L'\0'
    );
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        s.data(),
        int(s.size()),
        out.data(),
        n
    );

    return out;
}

/**
 * Executes native HTTPS requests with bounded responses and cooperative cancellation.
 */
class NativeHttp final : public Http {

    /**
     * Performs HTTPS requests with cancellation, response limits, progress, and streaming reception.
     *
     * @param[in] input The request URL, timeout, response-size limit, and streaming receive callback.
     * @param[in] cancel The cancellation predicate, checked periodically during native transport.
     *
     * @return The HTTP status, headers, and nonstreamed body; transport failures throw an SDK exception.
     */
    HttpResponse request(
        const HttpRequest& input,
        const Cancel& cancel
    ) override {
        const auto url = wide(input.url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = DWORD(-1);
        parts.dwUrlPathLength = DWORD(-1);
        parts.dwExtraInfoLength = DWORD(-1);
        require(
            WinHttpCrackUrl(
                url.c_str(),
                DWORD(url.size()),
                0,
                &parts
            ),
            XMAX_INVALID_ARGUMENT,
            "Invalid API URL"
        );

        const std::wstring host(
            parts.lpszHostName,
            parts.dwHostNameLength
        );
        std::wstring target(
            parts.lpszUrlPath,
            parts.dwUrlPathLength
        );

        if (parts.dwExtraInfoLength)
            target.append(
                parts.lpszExtraInfo,
                parts.dwExtraInfoLength
            );

        InternetHandle session(WinHttpOpen(
            wide(std::string("XmaxSDK/") + XMAX_VERSION).c_str(),
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        ));
        int timeout = int(input.timeout_ms);
        WinHttpSetTimeouts(
            session,
            timeout,
            timeout,
            timeout,
            timeout
        );

        InternetHandle connection(WinHttpConnect(
            session,
            host.c_str(),
            parts.nPort,
            0
        ));
        InternetHandle req(WinHttpOpenRequest(
            connection,
            wide(input.method).c_str(),
            target.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        ));
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(
            req,
            WINHTTP_OPTION_REDIRECT_POLICY,
            &redirect,
            sizeof(redirect)
        );

        std::string header_text;

        for (const auto& header : input.headers)
            header_text += header.first + ": " + header.second + "\r\n";

        const auto headers = wide(header_text);
        const auto& data = input.body;
        require(
            !cancel(),
            XMAX_CANCELLED,
            "Operation cancelled"
        );
        http_checked(
            WinHttpSendRequest(
                req,
                headers.c_str(),
                DWORD(headers.size()),
                WINHTTP_NO_REQUEST_DATA,
                0,
                DWORD(data.size()),
                0
            ),
            "HTTPS send"
        );

        size_t sent = 0;

        while (sent < data.size()) {
            require(
                !cancel(),
                XMAX_CANCELLED,
                "Operation cancelled"
            );

            const auto count = DWORD(std::min(size_t(65536), data.size() - sent));
            DWORD written = 0;
            http_checked(
                WinHttpWriteData(
                    req,
                    data.data() + sent,
                    count,
                    &written
                ),
                "HTTPS upload"
            );
            require(
                written > 0,
                XMAX_NETWORK_ERROR,
                "HTTPS upload made no progress"
            );
            sent += written;

            if (input.progress)
                input.progress(sent, data.size());
        }

        http_checked(
            WinHttpReceiveResponse(
                req,
                nullptr
            ),
            "HTTPS response"
        );

        DWORD status = 0, size = sizeof(status);
        require(
            WinHttpQueryHeaders(
                req,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status,
                &size,
                WINHTTP_NO_HEADER_INDEX
            ),
            XMAX_NETWORK_ERROR,
            "Cannot read HTTP status"
        );

        HttpResponse response;
        response.status = long(status);

        for (const auto& item : std::vector<std::pair<DWORD, const wchar_t*>>{
                 {WINHTTP_QUERY_ETAG, L"ETag"},
                 {WINHTTP_QUERY_CONTENT_LENGTH, L"Content-Length"}
             }) {
            DWORD bytes = 0;
            WinHttpQueryHeaders(
                req,
                item.first,
                WINHTTP_HEADER_NAME_BY_INDEX,
                nullptr,
                &bytes,
                WINHTTP_NO_HEADER_INDEX
            );

            if (!bytes)
                continue;

            std::wstring value(
                bytes / sizeof(wchar_t),
                L'\0'
            );

            if (WinHttpQueryHeaders(
                req,
                item.first,
                WINHTTP_HEADER_NAME_BY_INDEX,
                value.data(),
                &bytes,
                WINHTTP_NO_HEADER_INDEX
            )) {
                value.resize(bytes / sizeof(wchar_t));

                while (!value.empty() && value.back() == 0)
                    value.pop_back();

                const int count = WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    value.data(),
                    int(value.size()),
                    nullptr,
                    0,
                    nullptr,
                    nullptr
                );
                std::string text(
                    size_t(count),
                    '\0'
                );
                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    value.data(),
                    int(value.size()),
                    text.data(),
                    count,
                    nullptr,
                    nullptr
                );
                response.headers.emplace_back(
                    item.first == WINHTTP_QUERY_ETAG ? "ETag" : "Content-Length",
                    std::move(text)
                );
            }
        }

        uint64_t total = 0, consumed = 0;

        for (const auto& header : response.headers)
            if (header.first == "Content-Length") {
                try {
                    total = std::stoull(header.second);
                } catch (...) {
                }
            }

        require(
            total <= input.response_limit,
            XMAX_NETWORK_ERROR,
            "HTTP response exceeds the byte limit"
        );

        char buffer[16384];
        DWORD received = 0;

        do {
            require(
                !cancel(),
                XMAX_CANCELLED,
                "Operation cancelled"
            );
            http_checked(
                WinHttpReadData(
                    req,
                    buffer,
                    sizeof(buffer),
                    &received
                ),
                "HTTP response read"
            );
            require(
                consumed + received <= input.response_limit,
                XMAX_NETWORK_ERROR,
                "HTTP response exceeds the byte limit"
            );

            if (received) {
                if (input.receive)
                    input.receive(buffer, received);
                else
                    response.body.append(
                        buffer,
                        received
                    );
                consumed += received;
                if (data.empty() && input.progress)
                    input.progress(consumed, total);
            }
        } while (received);

        require(
            total == 0 || consumed == total,
            XMAX_NETWORK_ERROR,
            "Incomplete HTTP response body"
        );

        return response;
    }
};
#else

/**
 * Executes native HTTPS requests with bounded responses and cooperative cancellation.
 */
class NativeHttp final : public Http {

    /**
     * Performs HTTPS requests with cancellation, response limits, progress, and streaming reception.
     *
     * @param[in] input The request URL, timeout, response-size limit, and streaming receive callback.
     * @param[in] cancel The cancellation predicate, checked periodically during native transport.
     *
     * @return The HTTP status, headers, and nonstreamed body; transport failures throw an SDK exception.
     */
    HttpResponse request(
        const HttpRequest& input,
        const Cancel& cancel
    ) override {
        static const int initialized = [] { return curl_global_init(CURL_GLOBAL_DEFAULT); }();
        require(
            initialized == CURLE_OK,
            XMAX_NETWORK_ERROR,
            "Cannot initialize HTTPS transport"
        );

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(
            curl_easy_init(),
            curl_easy_cleanup
        );
        require(
            bool(handle),
            XMAX_NETWORK_ERROR,
            "Cannot create HTTPS request"
        );

        const auto& url = input.url;
        const auto& data = input.body;

        /**
         * Retains streamed transfer state and failures for reporting outside native callback boundaries.
         */
        struct Transfer {

            /**
             * The borrowed request description, valid until the synchronous request returns.
             */
            const HttpRequest& input;

            /**
             * The borrowed cancellation predicate, valid until the synchronous request returns.
             */
            const Cancel& cancel;

            /**
             * The HTTP response being accumulated by the native transfer.
             */
            HttpResponse response;

            /**
             * The received byte count used for size limits and progress.
             */
            uint64_t received = 0;

            /**
             * The exception captured by a native callback and rethrown on the C++ call stack.
             */
            std::exception_ptr failure;

        } transfer{input, cancel, {}, 0, {}};

        curl_slist* list = nullptr;

        for (const auto& header : input.headers) {
            const auto line = header.first + ": " + header.second;
            list = curl_slist_append(
                list,
                line.c_str()
            );
        }

        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
            list,
            curl_slist_free_all
        );
        curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, input.method.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, long(input.timeout_ms));
        curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);

        if (!data.empty()) {
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, data.data());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, long(data.size()));
        }

        curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(
            handle.get(),
            CURLOPT_WRITEFUNCTION,
            +[](char* bytes, size_t size, size_t count, void* context) -> size_t {
                auto& value = *static_cast<Transfer*>(context);
                const auto length = size * count;
                try {
                    require(
                        length <= value.input.response_limit &&
                            value.received <= value.input.response_limit - length,
                        XMAX_NETWORK_ERROR,
                        "HTTP response exceeds the byte limit"
                    );
                    if (value.input.receive)
                        value.input.receive(bytes, length);
                    else
                        value.response.body.append(bytes, length);
                    value.received += length;
                    return length;
                } catch (...) {
                    value.failure = std::current_exception();
                    return 0;
                }
            }
        );
        curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &transfer);
        curl_easy_setopt(
            handle.get(),
            CURLOPT_HEADERFUNCTION,
            +[](char* bytes, size_t size, size_t count, void* context) -> size_t {
                auto& value = *static_cast<Transfer*>(context);
                const auto length = size * count;
                try {
                    std::string line(bytes, length);
                    const auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        auto text = line.substr(colon + 1);
                        const auto first = text.find_first_not_of(" \t");
                        text = first == std::string::npos ? "" : text.substr(first);
                        while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
                            text.pop_back();
                        value.response.headers.emplace_back(line.substr(0, colon), std::move(text));
                    }
                    return length;
                } catch (...) {
                    value.failure = std::current_exception();
                    return 0;
                }
            }
        );
        curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &transfer);
        curl_easy_setopt(
            handle.get(),
            CURLOPT_XFERINFOFUNCTION,
            +[](void* context, curl_off_t total_down, curl_off_t down, curl_off_t total_up, curl_off_t up)
                -> int {
                auto& value = *static_cast<Transfer*>(context);
                try {
                    if (value.cancel())
                        return 1;
                    if (value.input.progress) {
                        if (value.input.body.empty())
                            value.input.progress(uint64_t(down), uint64_t(total_down));
                        else
                            value.input.progress(uint64_t(up), uint64_t(total_up));
                    }
                    return 0;
                } catch (...) {
                    value.failure = std::current_exception();
                    return 1;
                }
            }
        );

        const auto result = curl_easy_perform(handle.get());
        require(
            !cancel(),
            XMAX_CANCELLED,
            "Operation cancelled"
        );

        if (transfer.failure)
            std::rethrow_exception(transfer.failure);

        require(
            result == CURLE_OK,
            XMAX_NETWORK_ERROR,
            "HTTPS request failed (libcurl " + std::to_string(int(result)) +
                "): " + curl_easy_strerror(result)
        );

        long status = 0;
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
        transfer.response.status = status;

        return std::move(transfer.response);
    }
};
#endif

/**
 * Creates the platform's HTTPS byte transport.
 *
 * @return A shared HTTP transport for the current platform.
 */
std::shared_ptr<Http> make_http() {
    return std::make_shared<NativeHttp>();
}
} // namespace xmax::detail
