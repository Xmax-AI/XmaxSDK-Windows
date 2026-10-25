//
//  ExternalAudioPacket.hpp
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
 * A mono 48 kHz PCM16 packet timestamped on the external source's monotonic timeline.
 */
struct ExternalAudioPacket {

    /**
     * The 480 copied PCM16 samples representing 10 ms of audio.
     */
    std::vector<int16_t> samples;

    /**
     * The first sample's presentation timestamp in microseconds, shared with input video.
     */
    int64_t timestamp_us = 0;
};

} // namespace xmax::detail
