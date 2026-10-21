//
//  MessageCodec.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/MessageCodec.hpp"

namespace xmax::detail {

/**
 * 将房间 JSON 编码为共享事件标识的完整 UTF-8 分片。
 *
 * @param[in] message 完整的 UTF-8 JSON 信令文本，长度须在 [1, 262144] 字节内。
 * @param[in] id 所有分片共享的事件标识，用于接收方按事件重组；未分片时不使用。
 * @return 按顺序发送的 UTF-8 信令分片集合。
 */
std::vector<std::string> encode_message(
    const std::string& message,
    const std::string& id
) {
    require(
        !message.empty() && message.size() <= 262144, XMAX_INVALID_ARGUMENT,
        "RTC message exceeds 262144 bytes"
    );
    // 分片前先解析 JSON，同时校验 UTF-8 文本完整性。
    (void)json::parse(message);

    if (message.size() <= 800)
        return {message};

    std::vector<std::string> chunks;
    std::string chunk;
    size_t escaped = 0;

    for (size_t i = 0; i < message.size();) {
        const unsigned char c = static_cast<unsigned char>(message[i]);
        size_t n = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        const std::string scalar = message.substr(i, n);
        size_t bytes = json(scalar).dump().size() - 2;

        if (escaped + bytes > 600) {
            chunks.push_back(chunk);
            chunk.clear();
            escaped = 0;
        }

        chunk += scalar;
        escaped += bytes;
        i += n;
    }

    if (!chunk.empty())
        chunks.push_back(chunk);

    require(chunks.size() <= 512, XMAX_INVALID_ARGUMENT, "Too many RTC message fragments");

    std::vector<std::string> packets;

    for (size_t i = 0; i < chunks.size(); ++i)
        packets.push_back(
            json{
                {"event", "__agora_chunk__"},
                {"eventId", id},
                {"index", i},
                {"count", chunks.size()},
                {"data", chunks[i]}
            }.dump()
        );

    return packets;
}
} // namespace xmax::detail
