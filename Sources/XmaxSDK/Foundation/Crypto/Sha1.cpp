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
 * Encodes digest bytes as lowercase hexadecimal text.
 *
 * @param[in] data The digest bytes to encode.
 * @param[in] length The number of digest bytes.
 *
 * @return Lowercase hexadecimal text, with two characters per input byte.
 */
static std::string hex(
    const uint8_t* data,
    size_t length
) {
    const char* digits = "0123456789abcdef";
    std::string result(
        length * 2,
        '0'
    );

    for (size_t i = 0; i < length; ++i) {
        result[i * 2] = digits[data[i] >> 4];
        result[i * 2 + 1] = digits[data[i] & 15];
    }

    return result;
}

/**
 * Computes SHA-1 or HMAC-SHA1 using the system crypto backend.
 *
 * @param[in] data The complete bytes to hash.
 * @param[in] key The HMAC key; null selects unkeyed SHA-1.
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
            &algorithm,
            BCRYPT_SHA1_ALGORITHM,
            nullptr,
            key ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0
        ) == 0,
        XMAX_INTERNAL_ERROR,
        "Cannot initialize COS request signing"
    );

    const auto created = BCryptCreateHash(
        algorithm,
        &hash,
        nullptr,
        0,
        key ? reinterpret_cast<PUCHAR>(const_cast<char*>(key->data())) : nullptr,
        key ? ULONG(key->size()) : 0,
        0
    );
    const bool success = created == 0 &&
                         BCryptHashData(
                             hash,
                             reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                             ULONG(data.size()),
                             0
                         ) == 0 &&
                         BCryptFinishHash(
                             hash,
                             output,
                             sizeof(output),
                             0
                         ) == 0;

    if (hash)
        BCryptDestroyHash(hash);

    BCryptCloseAlgorithmProvider(
        algorithm,
        0
    );
    require(
        success,
        XMAX_INTERNAL_ERROR,
        "COS request signing failed"
    );
#else
    if (key)
        CCHmac(
            kCCHmacAlgSHA1,
            key->data(),
            key->size(),
            data.data(),
            data.size(),
            output
        );
    else
        CC_SHA1(
            data.data(),
            CC_LONG(data.size()),
            output
        );
#endif

    return hex(
        output,
        sizeof(output)
    );
}

/**
 * Computes the input's lowercase hexadecimal SHA-1 digest.
 *
 * @param[in] data The complete bytes used to compute the SHA-1 digest.
 */
std::string sha1_hex(
    const std::string& data
) {
    return digest(
        data,
        nullptr
    );
}

/**
 * Computes a lowercase hexadecimal HMAC-SHA1 signature for storage authentication.
 *
 * @param[in] key The HMAC-SHA1 key bytes.
 * @param[in] data The encoded bytes to process; they must remain valid until this synchronous call returns.
 */
std::string hmac_sha1_hex(
    const std::string& key,
    const std::string& data
) {
    return digest(
        data,
        &key
    );
}
} // namespace xmax::detail
