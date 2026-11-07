//
//  StorageFileName.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <string>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CFCharacterSet.h>
#else
#include <cwctype>
#include <locale.h>
#endif

namespace xmax::detail {

/** Checks a Unicode scalar against the platform's alphanumeric categories. */
inline bool storage_name_alphanumeric(char32_t value) {
#ifdef _WIN32
    wchar_t units[2]{};
    int length = 1;
    if (value < 0x10000) {
        units[0] = static_cast<wchar_t>(value);
    } else {
        value -= 0x10000;
        units[0] = static_cast<wchar_t>(0xd800 + (value >> 10));
        units[1] = static_cast<wchar_t>(0xdc00 + (value & 0x3ff));
        length = 2;
    }
    WORD types[2]{};
    if (GetStringTypeW(CT_CTYPE1, units, length, types) && ((types[0] | types[1]) & (C1_ALPHA | C1_DIGIT)))
        return true;
    return GetStringTypeW(CT_CTYPE3, units, length, types) &&
           ((types[0] | types[1]) & (C3_NONSPACING | C3_VOWELMARK));
#elif defined(__APPLE__)
    return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetAlphaNumeric), value);
#else
    static const auto locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
    return locale && iswalnum_l(value, locale);
#endif
}

/** Normalizes an object filename while preserving UTF-8 letters and digits. */
inline std::string storage_file_name(std::string_view input) {
    std::string result;
    while (!input.empty()) {
        const auto first = static_cast<unsigned char>(input.front());
        size_t length = first < 0x80 ? 1 : first >= 0xf0 ? 4 : first >= 0xe0 ? 3 : 2;
        char32_t value = first & (length == 1 ? 0x7f : 0x7f >> length);
        bool valid = length <= input.size() && (length == 1 || (first >= 0xc2 && first <= 0xf4));
        for (size_t index = 1; valid && index < length; ++index) {
            const auto next = static_cast<unsigned char>(input[index]);
            valid = (next & 0xc0) == 0x80;
            value = (value << 6) | (next & 0x3f);
        }
        valid = valid && !(length == 3 && value < 0x800) && !(length == 4 && value < 0x10000) &&
                !(value >= 0xd800 && value <= 0xdfff) && value <= 0x10ffff;
        if (!valid) length = 1;
        const bool retained = valid && (value == '.' || value == '-' || storage_name_alphanumeric(value));
        if (retained)
            result.append(input.substr(0, length));
        else if (result.empty() || result.back() != '_')
            result += '_';
        input.remove_prefix(length);
    }
    const auto first = result.find_first_not_of("._-");
    return first == std::string::npos ? std::string{} : result.substr(first, result.find_last_not_of("._-") - first + 1);
}

} // namespace xmax::detail
