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
 * 根据模型配置执行经过鉴权的 API 请求，并统一解析服务响应和错误。
 */
class ApiService {

  public:

    /**
     * 校验并保存 API 配置、HTTP 传输器及凭证脱敏保护对象。
     *
     * @param[in] config 复制后的 API 凭证、模型与请求超时配置。
     * @param[in] http 由当前服务保留的共享 HTTP 字节传输器。
     */
    explicit ApiService(
        Config config,
        std::shared_ptr<Http> http = make_http()
    );

    /**
     * 对相对 API 请求进行鉴权，校验响应封装并返回数据负载。
     *
     * @param[in] method HTTP 请求方法。
     * @param[in] path 相对当前模型 API 根地址的资源路径。
     * @param[in] body 可选 JSON 请求体，空指针表示不发送请求体。
     * @param[in] cancelled 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] realtime 是否使用实时会话 API 地址。
     * @return 服务响应中已通过状态校验的 data 负载。
     */
    json request(
        const std::string& method,
        const std::string& path,
        const json* body,
        const Cancel& cancelled,
        bool realtime = false
    ) const;

    /**
     * 借用创建服务时复制的只读配置。
     *
     * @return 本服务拥有的只读配置引用，在服务销毁前有效。
     */
    const Config& config() const {
        return config_;
    }

    /**
     * 共享底层字节传输器，供不使用 JSON 响应封装的存储请求使用。
     *
     * @return 共享的底层 HTTP 字节传输器引用。
     */
    std::shared_ptr<Http> transport() const {
        return http_;
    }

  private:

    /**
     * 创建服务时复制的 API 配置。
     */
    Config config_;

    /**
     * 共享的底层 HTTPS 字节传输器。
     */
    std::shared_ptr<Http> http_;

    /**
     * 保持 API 凭证在日志脱敏集合中注册的保护对象。
     */
    std::shared_ptr<const std::string> log_key_;
};
} // namespace xmax::detail
