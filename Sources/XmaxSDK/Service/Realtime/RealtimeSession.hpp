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
 * The server-session identifier and validated RTC connection information.
 */
struct RealtimeSession {

    /**
     * The server-session identifier used for keepalive, closing, and failure rollback.
     */
    std::string id;

    /**
     * The RTC connection information allocated by the session.
     */
    Connection room;
};
} // namespace xmax::detail
