//
//  RtcCallbacks.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include "Foundation/Media/AudioFrame.hpp"

namespace xmax::detail {

/**
 * Delivers RTC frames, events, failures, and first-publication notifications to realtime control.
 */
struct RtcCallbacks {

    /**
     * The owned-frame callback, invoked on the native media thread.
     */
    std::function<void(Frame)> frame;

    /** Invalidates the result binding when the selected remote user stops publishing video. */
    std::function<void()> video_unpublished;

    /**
     * The receiver for RTC state, statistics, and network-quality events.
     */
    std::function<void(xmax_event)> event;

    /**
     * The native RTC failure callback, invoked on a native RTC thread.
     */
    std::function<void(Error)> failure;

    /**
     * The first-actual-publication notification, distinct from successful frame submission.
     */
    std::function<void()> published;

    /**
     * The copied result-user PCM callback, invoked before remote-user mixing on the native media thread.
     */
    std::function<void(AudioFrame)> audio;

    /** A sampled physical microphone volume in the range 0..255. */
    std::function<void(int)> microphone_level;

    /**
     * The full directional video statistics, including each native processing-stage frame rate.
     */
    std::function<void(xmax_video_statistics)> statistics;

    /**
     * The copied room-signaling sender account and UTF-8 payload, delivered on a native RTC thread.
     */
    std::function<void(std::string, std::string)> message;
};
} // namespace xmax::detail
