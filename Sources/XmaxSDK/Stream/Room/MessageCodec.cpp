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
 * Encodes room JSON as complete UTF-8 fragments sharing an event identifier.
 *
 * @param[in] message The complete UTF-8 JSON signaling text, between 1 and 262144 bytes.
 * @param[in] id The identifier shared by fragments for receiver reassembly; unused for unfragmented
 * messages.
 *
 * @return The ordered UTF-8 signaling fragments.
 */
std::vector<std::string> encode_message(
    const std::string& message,
    const std::string& id
) {
    require(
        !message.empty() && message.size() <= 262144,
        XMAX_INVALID_ARGUMENT,
        "RTC message exceeds 262144 bytes"
    );
    // Parse JSON before fragmentation to validate complete UTF-8 input.
    (void)json::parse(message);

    if (message.size() <= 800)
        return {message};

    std::vector<std::string> chunks;
    std::string chunk;
    size_t escaped = 0;

    for (size_t i = 0; i < message.size();) {
        const unsigned char c = static_cast<unsigned char>(message[i]);
        size_t n = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        const std::string scalar = message.substr(
            i,
            n
        );
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

    require(
        chunks.size() <= 512,
        XMAX_INVALID_ARGUMENT,
        "Too many RTC message fragments"
    );

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
