//
//  RoomMessageDecoder.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomMessageDecoder.hpp"
#include <algorithm>

namespace xmax::detail {

std::optional<json> RoomMessageDecoder::decode(
    const std::string& sender,
    const std::string& data
) {
    if (data.size() > 262144)
        return {};
    const auto message = json::parse(data, nullptr, false);
    if (!message.is_object())
        return {};
    const auto event = message.find("event");
    if (event == message.end() || (*event != "__agora_chunk__" && *event != "__trtc_chunk__"))
        return message;
    if (!message.contains("eventId") || !message["eventId"].is_string() ||
        message["eventId"].get_ref<const std::string&>().empty() || !message.contains("index") ||
        !message["index"].is_number_integer() || !message.contains("count") ||
        !message["count"].is_number_integer() || !message.contains("data") || !message["data"].is_string())
        return {};

    // Compare in JSON's numeric domain before narrowing unsigned or oversized integers.
    if (message["count"] < 1 || message["count"] > 512 || message["index"] < 0 ||
        message["index"] >= message["count"])
        return {};

    const auto count = message["count"].get<size_t>();
    const auto index = message["index"].get<size_t>();
    const auto key = std::make_pair(sender, message["eventId"].get<std::string>());
    auto inserted = records_.try_emplace(key, Record{count, ++sequence_, {}, 0});
    auto& record = inserted.first->second;
    if (record.count != count)
        return {};

    const auto part = message["data"].get<std::string>();
    const auto previous = record.parts.find(index);
    const auto old_size = previous == record.parts.end() ? 0 : previous->second.size();
    if (record.bytes - old_size + part.size() > 262144) {
        bytes_ -= record.bytes;
        records_.erase(key);
        return {};
    }

    record.bytes = record.bytes - old_size + part.size();
    bytes_ = bytes_ - old_size + part.size();
    record.parts[index] = part;

    while (records_.size() > 1000 || bytes_ > 32 * 1024 * 1024) {
        const auto oldest =
            std::min_element(records_.begin(), records_.end(), [](const auto& a, const auto& b) {
                return a.second.order < b.second.order;
            });
        bytes_ -= oldest->second.bytes;
        records_.erase(oldest);
    }

    const auto found = records_.find(key);
    if (found == records_.end() || found->second.parts.size() != count)
        return {};
    std::string joined;
    joined.reserve(found->second.bytes);
    for (const auto& piece : found->second.parts)
        joined += piece.second;
    bytes_ -= found->second.bytes;
    records_.erase(found);
    auto result = json::parse(joined, nullptr, false);
    return result.is_object() ? std::optional<json>(std::move(result)) : std::nullopt;
}
} // namespace xmax::detail
