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

    /**
     * The full directional video statistics, including each native processing-stage frame rate.
     */
    std::function<void(xmax_video_statistics)> statistics;
};
} // namespace xmax::detail
