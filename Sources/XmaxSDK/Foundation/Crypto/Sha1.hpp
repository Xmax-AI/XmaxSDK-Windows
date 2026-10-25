//
//  Sha1.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * Computes the input's lowercase hexadecimal SHA-1 digest.
 *
 * @param[in] data The complete bytes used to compute the SHA-1 digest.
 */
std::string sha1_hex(
    const std::string& data
);

/**
 * Computes a lowercase hexadecimal HMAC-SHA1 signature for storage authentication.
 *
 * @param[in] key The HMAC-SHA1 key bytes.
 * @param[in] data The encoded bytes to process; they must remain valid until this synchronous call returns.
 */
std::string hmac_sha1_hex(
    const std::string& key,
    const std::string& data
);
} // namespace xmax::detail
