//
//  VideoEncodingConfiguration.hpp
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
 * The resolved encoding dimensions, frame rate, bitrate limits, and degradation preference.
 */
struct VideoEncodingConfiguration {

    /**
     * The native encoder's current pixel dimensions and frame rate.
     */
    xmax_video_format format;

    /**
     * The effective minimum encoding bitrate, in kbps.
     */
    int minimum_bitrate = 100;

    /**
     * The effective maximum encoding bitrate, in kbps.
     */
    int maximum_bitrate = 100;

    /**
     * The encoding degradation preference, using XMAX_ENCODER_* constants.
     */
    int preference = XMAX_ENCODER_MAINTAIN_FRAMERATE;

    /**
     * Whether RTC horizontally mirrors uplink encoding; local preview is unaffected.
     */
    bool mirrored = false;
};
} // namespace xmax::detail
