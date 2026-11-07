//
//  Utf8.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string_view>

namespace xmax::detail {

/** Trims UTF-8 whitespace and newline characters from both ends of the text. */
inline std::string_view trim_utf8_whitespace(std::string_view text) {
    constexpr std::string_view spaces[] = {
        " ", "\t", "\n", "\v", "\f", "\r", u8"\u0085", u8"\u00a0", u8"\u1680",
        u8"\u2000", u8"\u2001", u8"\u2002", u8"\u2003", u8"\u2004", u8"\u2005",
        u8"\u2006", u8"\u2007", u8"\u2008", u8"\u2009", u8"\u200a", u8"\u200b",
        u8"\u2028", u8"\u2029", u8"\u202f", u8"\u205f", u8"\u3000"
    };
    for (;;) {
        const auto space = std::find_if(std::begin(spaces), std::end(spaces), [text](auto item) {
            return text.substr(0, item.size()) == item;
        });
        if (space == std::end(spaces))
            break;
        text.remove_prefix(space->size());
    }
    for (;;) {
        const auto space = std::find_if(std::begin(spaces), std::end(spaces), [text](auto item) {
            return text.size() >= item.size() && text.substr(text.size() - item.size()) == item;
        });
        if (space == std::end(spaces))
            return text;
        text.remove_suffix(space->size());
    }
}

/**
 * Copies UTF-8 text into a fixed-capacity buffer without splitting its final code point.
 *
 * @param[out] destination The writable buffer; may be null only when capacity is zero.
 * @param[in] capacity The buffer capacity in bytes, including the trailing NUL.
 * @param[in] text The UTF-8 text to copy; bytes beyond capacity are omitted.
 */
inline void copy_utf8(
    char* destination,
    size_t capacity,
    std::string_view text
) {
    if (capacity == 0)
        return;
    auto count = std::min(capacity - 1, text.size());
    while (count && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80)
        --count;
    if (count)
        std::memcpy(
            destination,
            text.data(),
            count
        );
    destination[count] = 0;
}
} // namespace xmax::detail
