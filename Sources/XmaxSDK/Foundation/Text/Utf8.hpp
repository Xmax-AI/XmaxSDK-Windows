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
#include <string_view>

namespace xmax::detail {

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
