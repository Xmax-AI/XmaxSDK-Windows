//
//  Sha1.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Crypto/Sha1.hpp"
#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#endif
namespace xmax::detail {

/**
 * 将摘要字节编码为小写十六进制文本。
 *
 * @param[in] data 参与十六进制编码的摘要字节指针。
 * @param[in] length 摘要字节数。
 * @return 小写十六进制文本，每个输入字节对应两个字符。
 */
static std::string hex(
    const uint8_t* data,
    size_t length
) {
    const char* digits = "0123456789abcdef";
    std::string result(length * 2, '0');

    for (size_t i = 0; i < length; ++i) {
        result[i * 2] = digits[data[i] >> 4];
        result[i * 2 + 1] = digits[data[i] & 15];
    }

    return result;
}

/**
 * 使用系统加密后端计算 SHA-1 或 HMAC-SHA1 摘要。
 *
 * @param[in] data 参与摘要计算的完整字节内容。
 * @param[in] key HMAC 密钥；空指针表示执行不带密钥的 SHA-1。
 */
static std::string digest(
    const std::string& data,
    const std::string* key
) {
    uint8_t output[20];
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    require(
        BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, key ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0
        ) == 0,
        XMAX_INTERNAL_ERROR, "Cannot initialize COS request signing"
    );

    const auto created = BCryptCreateHash(
        algorithm, &hash, nullptr, 0,
        key ? reinterpret_cast<PUCHAR>(const_cast<char*>(key->data())) : nullptr,
        key ? ULONG(key->size()) : 0, 0
    );
    const bool success =
        created == 0 &&
        BCryptHashData(
            hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), ULONG(data.size()), 0
        ) == 0 &&
        BCryptFinishHash(hash, output, sizeof(output), 0) == 0;

    if (hash)
        BCryptDestroyHash(hash);

    BCryptCloseAlgorithmProvider(algorithm, 0);
    require(success, XMAX_INTERNAL_ERROR, "COS request signing failed");
#else
    if (key)
        CCHmac(kCCHmacAlgSHA1, key->data(), key->size(), data.data(), data.size(), output);
    else
        CC_SHA1(data.data(), CC_LONG(data.size()), output);
#endif

    return hex(output, sizeof(output));
}

/**
 * 计算输入字节的 SHA-1 小写十六进制摘要。
 *
 * @param[in] data 参与 SHA-1 摘要计算的完整字节内容。
 */
std::string sha1_hex(
    const std::string& data
) {
    return digest(data, nullptr);
}

/**
 * 计算用于存储鉴权的 HMAC-SHA1 小写十六进制签名。
 *
 * @param[in] key 用于 HMAC-SHA1 计算的密钥字节。
 * @param[in] data 待处理的编码字节，本次同步调用返回前必须有效。
 */
std::string hmac_sha1_hex(
    const std::string& key,
    const std::string& data
) {
    return digest(data, &key);
}
} // namespace xmax::detail
