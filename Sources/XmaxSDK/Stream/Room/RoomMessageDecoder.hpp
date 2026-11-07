//
//  RoomMessageDecoder.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Json.hpp"
#include <map>
#include <optional>

namespace xmax::detail {

/**
 * Reassembles bounded provider fragments, separating partial messages by sender.
 */
class RoomMessageDecoder {

  public:

    /**
     * Decodes plain JSON or a completed Agora/TRTC fragment sequence.
     */
    std::optional<json> decode(
        const std::string& sender,
        const std::string& data
    );

  private:

    /**
     * The retained pieces and insertion order of one incomplete message.
     */
    struct Record {
        /**
         * The number of expected fragments, bounded to 512.
         */
        size_t count;

        /**
         * The insertion order used to evict the oldest incomplete message.
         */
        uint64_t order;

        /**
         * Received UTF-8 slices indexed by their zero-based position.
         */
        std::map<size_t, std::string> parts;

        /**
         * The retained payload size in bytes, bounded to 262144.
         */
        size_t bytes = 0;
    };

    /**
     * The incomplete messages keyed by sender and provider fragment ID.
     */
    std::map<std::pair<std::string, std::string>, Record> records_;

    /**
     * The retained aggregate payload size, bounded to 32 MiB.
     */
    size_t bytes_ = 0;

    /**
     * The monotonic insertion sequence for incomplete-message eviction.
     */
    uint64_t sequence_ = 0;
};
} // namespace xmax::detail
