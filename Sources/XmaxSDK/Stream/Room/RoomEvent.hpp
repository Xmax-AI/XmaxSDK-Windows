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
 * 构造携带房间、任务、视频规格与生成条件的信令。
 *
 * @param[in] event 生成信令的事件名称。
 * @param[in] c 当前 RTC 房间连接信息。
 * @param[in] task 当前生成任务标识；控制信令允许省略时为空文本。
 * @param[in] format 服务端生成画布尺寸和帧率，由模型规则解析。
 * @param[in] prompt UTF-8 生成提示词。
 * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
 * @param[in] face 是否启用生成条件中的人脸修复。
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
 * 构造携带房间信息和可选任务标识的控制信令。
 *
 * @param[in] event 房间控制信令的事件名称。
 * @param[in] connection 当前 RTC 房间连接信息。
 * @param[in] task 当前生成任务标识；控制信令允许省略时为空文本。
 */
json room_event(
    const std::string& event,
    const Connection& connection,
    const std::string& task = {}
);
} // namespace xmax::detail
