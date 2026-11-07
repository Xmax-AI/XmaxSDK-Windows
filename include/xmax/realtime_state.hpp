//
//  realtime_state.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once

#include "error.hpp"
#include <optional>
#include <string>
#include <variant>

namespace xmax {

/** The current connection and generation phase. */
enum class realtime_connection_state {
    idle = XMAX_IDLE,
    preparing = XMAX_PREPARING,
    ready = XMAX_READY,
    connecting = XMAX_CONNECTING,
    connected = XMAX_CONNECTED,
    generating = XMAX_GENERATING,
    disconnecting = XMAX_DISCONNECTING
};

/** Explicit stop reasons that callers can request. */
enum class realtime_stop_reason {
    normal = XMAX_REASON_NORMAL,
    orientation_changed = XMAX_REASON_ORIENTATION_CHANGED
};

/** A normal stop reason or an SDK failure, including a caller-supplied disconnect reason. */
using realtime_reason = std::variant<realtime_stop_reason, error>;

/** A snapshot of connection state, optional business identifiers, and optional reason. */
struct realtime_state {
    realtime_connection_state connection_state;
    std::optional<std::string> session_id;
    std::optional<std::string> task_id;
    std::optional<realtime_reason> reason;

    /** Converts a C state snapshot or a state event from the polling interface. */
    static realtime_state from_event(const xmax_event_info& info) {
        realtime_state snapshot{
            static_cast<realtime_connection_state>(info.event.state),
            info.event.session_id[0] ? std::optional<std::string>{info.event.session_id} : std::nullopt,
            info.event.task_id[0] ? std::optional<std::string>{info.event.task_id} : std::nullopt,
            std::nullopt
        };
        switch (info.reason) {
        case XMAX_REASON_NORMAL:
            snapshot.reason = realtime_stop_reason::normal;
            break;
        case XMAX_REASON_ORIENTATION_CHANGED:
            snapshot.reason = realtime_stop_reason::orientation_changed;
            break;
        case XMAX_REASON_FAILURE:
            snapshot.reason = error(info.error.code, info.error.message, info.error);
            break;
        default:
            break;
        }
        return snapshot;
    }
};

} // namespace xmax
