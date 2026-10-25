//
//  RuntimeInfo.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Runtime/RuntimeInfo.hpp"
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <stdlib.h>
#include <sys/utsname.h>
#endif
namespace xmax::detail {

/**
 * Builds SDK, system, and device metadata for server-session creation.
 */
json runtime_info() {
    std::string os = "unknown", device = "unknown";
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    device = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64" : "x64";
    os = "Windows";
#else
    utsname info{};

    if (uname(&info) == 0) {
        os = info.release;
        device = info.machine;
    }
#endif

    // The service identifies this SDK by the fixed Windows product protocol identifier.
    return {
        {"platform", "windows"},
        {"os_version", os},
        {"sdk_version", XMAX_VERSION},
        {"device_model", device}
    };
}

/**
 * Generates a random identifier for signaling correlation and fragmentation.
 */
std::string random_id() {
    uint8_t bytes[16];
#ifdef _WIN32
    require(
        BCryptGenRandom(
            nullptr,
            bytes,
            sizeof(bytes),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        ) == 0,
        XMAX_INTERNAL_ERROR,
        "Random number generation failed"
    );
#else
    arc4random_buf(
        bytes,
        sizeof(bytes)
    );
#endif

    std::ostringstream out;

    for (auto byte : bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);

    return out.str();
}

/**
 * Generates a task identifier in the generation signaling format.
 */
std::string task_id() {
    const auto hex = random_id();
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded;
    unsigned accumulator = 0;
    int bits = 0;

    for (size_t i = 0; i < hex.size(); i += 2) {
        accumulator = (accumulator << 8) | unsigned(std::stoul(
            hex.substr(
                                                   i,
                                                   2
                                               ),
            nullptr,
            16
        ));
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            encoded += alphabet[(accumulator >> bits) & 63];
        }
    }

    if (bits)
        encoded += alphabet[(accumulator << (6 - bits)) & 63];

    return "task-" + encoded + "?os=windows";
}
} // namespace xmax::detail
