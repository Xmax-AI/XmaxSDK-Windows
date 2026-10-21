//
//  RealtimeSession.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/RTC/Connection.hpp"

namespace xmax::detail {

/**
 * 保存服务端会话标识和已验证的 RTC 连接信息。
 */
struct RealtimeSession {

    /**
     * 服务端会话标识，用于保活、关闭和失败回滚。
     */
    std::string id;

    /**
     * 服务端会话分配的 RTC 连接信息。
     */
    Connection room;
};
} // namespace xmax::detail
