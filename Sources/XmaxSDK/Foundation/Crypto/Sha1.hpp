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
 * 计算输入字节的 SHA-1 小写十六进制摘要。
 *
 * @param[in] data 参与 SHA-1 摘要计算的完整字节内容。
 */
std::string sha1_hex(
    const std::string& data
);

/**
 * 计算用于存储鉴权的 HMAC-SHA1 小写十六进制签名。
 *
 * @param[in] key 用于 HMAC-SHA1 计算的密钥字节。
 * @param[in] data 待处理的编码字节，本次同步调用返回前必须有效。
 */
std::string hmac_sha1_hex(
    const std::string& key,
    const std::string& data
);
} // namespace xmax::detail
