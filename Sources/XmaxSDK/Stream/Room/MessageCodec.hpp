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
 * Encodes room JSON as complete UTF-8 fragments sharing an event identifier.
 *
 * @param[in] message The complete UTF-8 JSON signaling text, between 1 and 262144 bytes.
 * @param[in] event_id The identifier shared by fragments for receiver reassembly; unused for unfragmented
 * messages.
 *
 * @return The ordered UTF-8 signaling fragments.
 */
std::vector<std::string> encode_message(
    const std::string& message,
    const std::string& event_id
);
} // namespace xmax::detail
