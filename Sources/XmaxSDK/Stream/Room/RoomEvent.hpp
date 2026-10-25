//
//  RoomEvent.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Json.hpp"
#include "Foundation/RTC/RtcPlatform.hpp"

namespace xmax::detail {

/**
 * Builds signaling with room, task, video format, and generation conditions.
 *
 * @param[in] event The generation signaling event name.
 * @param[in] c The current RTC room connection information.
 * @param[in] task The current generation task identifier; empty where control signaling permits omission.
 * @param[in] format The model-resolved generation canvas dimensions and frame rate.
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 */
json generation_event(
    const std::string& event,
    const Connection& c,
    const std::string& task,
    xmax_video_format format,
    const std::string& prompt,
    const std::string& reference,
    bool face
);

/**
 * Builds a room control message with an optional task identifier.
 *
 * @param[in] event The room control event name.
 * @param[in] connection The current RTC room connection information.
 * @param[in] task The current generation task identifier; empty where control signaling permits omission.
 */
json room_event(
    const std::string& event,
    const Connection& connection,
    const std::string& task = {}
);
} // namespace xmax::detail
