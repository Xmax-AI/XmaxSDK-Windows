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
 * 描述 HTTP 请求及其响应大小限制、进度回调、流式接收和超时配置。
 */
struct HttpRequest {

    /**
     * HTTP 请求方法。
     */
    std::string method;

    /**
     * 完整的 HTTPS 请求地址，需符合传输器的地址与重定向策略。
     */
    std::string url;

    /**
     * 本次请求要发送的完整字节内容。
     */
    std::string body;

    /**
     * HTTP 头名称与值的有序集合。
     */
    std::vector<std::pair<std::string, std::string>> headers;

    /**
     * HTTP 请求超时，单位为毫秒。
     */
    uint32_t timeout_ms = 15000;

    /**
     * 允许接收的最大响应字节数，超出时中止传输。
     */
    uint64_t response_limit = 4 * 1024 * 1024;

    /**
     * 传输进度回调，参数依次为已传输字节数与总字节数；未知总数为 0。
     */
    std::function<void(uint64_t, uint64_t)> progress;

    /**
     * 流式响应接收回调，参数依次为借用数据指针和字节数；仅在回调期间有效。
     */
    std::function<void(const char*, size_t)> receive;
};

/**
 * 保存 HTTP 状态码、响应头和已接收的响应体。
 */
struct HttpResponse {

    /**
     * 服务端返回的 HTTP 状态码。
     */
    long status = 0;

    /**
     * 已接收的响应字节；使用流式接收时可不在内存保存完整响应。
     */
    std::string body;

    /**
     * HTTP 头名称与值的有序集合。
     */
    std::vector<std::pair<std::string, std::string>> headers{};
};

/**
 * 定义同步 HTTP 请求接口，支持取消、响应大小限制和流式接收。
 */
class Http {

  public:

    /**
     * 在所有请求结束后释放字节传输器。
     *
     * @note 销毁前应结束使用本对象的并发调用。
     */
    virtual ~Http() = default;

    /**
     * 执行同步 HTTP 请求，支持取消、响应大小限制、传输进度和流式接收。
     *
     * @param[in] request 本次请求的 URL、超时、响应大小限制和流式接收回调。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @return HTTP 状态码、响应头和非流式响应体；传输失败时抛出 SDK 异常。
     */
    virtual HttpResponse request(
        const HttpRequest& request,
        const Cancel& cancel
    ) = 0;
};

/**
 * 创建当前平台的 HTTPS 字节传输器。
 *
 * @return 可共享的当前平台 HTTP 传输器。
 */
std::shared_ptr<Http> make_http();
} // namespace xmax::detail
