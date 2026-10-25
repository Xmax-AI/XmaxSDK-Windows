//
//  AudioFrame.hpp
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
 * Owns one result user's interleaved PCM16 packet and its native rendering clock sample.
 */
struct AudioFrame {

    /**
     * The copied little-endian signed PCM16 bytes.
     */
    std::vector<uint8_t> data;

    /**
     * The sampling frequency in hertz, validated in [8000, 192000].
     */
    uint32_t sample_rate = 0;

    /**
     * The interleaved channel count, validated as one or two.
     */
    uint32_t channels = 0;

    /**
     * The sample count in each channel, validated in [1, 192000].
     */
    uint32_t samples_per_channel = 0;

    /**
     * The native receiver rendering timestamp in milliseconds; nonpositive values break continuity.
     */
    int64_t render_time_ms = 0;
};
} // namespace xmax::detail
