//
//  MessageCodec.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Json.hpp"

namespace xmax::detail {

/**
 * 将房间 JSON 编码为共享事件标识的完整 UTF-8 分片。
 *
 * @param[in] message 完整的 UTF-8 JSON 信令文本，长度须在 [1, 262144] 字节内。
 * @param[in] event_id 所有分片共享的事件标识，用于接收方按事件重组；未分片时不使用。
 * @return 按顺序发送的 UTF-8 信令分片集合。
 */
std::vector<std::string> encode_message(
    const std::string& message,
    const std::string& event_id
);
} // namespace xmax::detail
