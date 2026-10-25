//
//  RealtimeState.hpp
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
 * The connection state and business identifiers belonging to one realtime lifecycle.
 */
struct RealtimeState {

    /**
     * The local media, connection, or generation phase.
     */
    xmax_state connection_state = XMAX_IDLE;

    /**
     * The current or most recently released server session identifier.
     */
    std::string session_id;

    /**
     * The active generation task identifier; empty outside generation.
     */
    std::string task_id;
};
} // namespace xmax::detail
