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
 * 校验服务端会话响应，提取会话标识与 RTC 连接信息。
 *
 * @param[in] payload 服务端会话响应的 data 对象，必须包含会话及 RTC 必需字段。
 * @return 包含服务端会话标识和有效 RTC 信息的会话记录。
 */
RealtimeSession parse_connection(
    const json& payload
);

/**
 * 管理服务端会话的创建、保活和关闭，并在部分创建失败时保留清理所需标识。
 */
class RealtimeSessionService {

  public:

    /**
     * 保留用于服务端会话创建、保活和关闭的共享 API 服务。
     */
    explicit RealtimeSessionService(
        std::shared_ptr<ApiService> api
    )
        : api_(std::move(api)) {
    }

    /**
     * 分配服务端会话并验证 RTC 信息，保留失败回滚所需的会话标识。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @return 已创建并验证连接信息的会话借用引用。
     */
    const RealtimeSession& create(
        const Cancel& cancel
    );

    /**
     * 刷新活动服务端会话，支持协作式取消。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void heartbeat(
        const Cancel& cancel
    );

    /**
     * 关闭活动服务端会话并清除本地会话记录。
     */
    void close();

    /**
     * 在生命周期执行器上借用当前会话记录。
     *
     * @return 活动会话的借用引用，修改或关闭会话后不得继续依赖原内容。
     */
    const RealtimeSession& active() const {
        return session_;
    }

  private:

    /**
     * 保留共享 API 服务，使子控制器在客户端句柄释放后仍可使用。
     */
    std::shared_ptr<ApiService> api_;

    /**
     * 当前活动服务端会话及其 RTC 连接信息；关闭后清空。
     */
    RealtimeSession session_;
};
} // namespace xmax::detail
