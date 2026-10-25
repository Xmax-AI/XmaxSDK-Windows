//
//  RtcStatsLogger.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {

/**
 * Formats RTC metrics, quality changes, and startup timing as filterable diagnostics.
 */
struct RtcStatsLogger {

    /**
     * Builds a credential-redacted diagnostic preserving the native RTC error code.
     *
     * @param[in] code The native RTC error code, preserved numerically in diagnostics.
     * @param[in] description The native error description; a null pointer uses the default diagnostic.
     */
    static std::string error_message(
        int code,
        const char* description
    );

    /**
     * Logs directional capture, codec, and send or render metrics while preserving unavailable values.
     *
     * @param[in] event The directional video statistics event; negative metrics mean unavailable.
     * @param[in] capture_fps The actual capture frame rate; negative if unavailable.
     * @param[in] codec_fps The actual encoding or decoding frame rate; negative if unavailable.
     */
    static void video(
        const xmax_event& event,
        int capture_fps = -1,
        int codec_fps = -1
    ) noexcept;

    /**
     * Logs startup timing, network quality, quality adjustments, and performance events in the configured
     * language.
     *
     * @param[in] e The startup timing, network quality, or performance event to log.
     */
    static void event(
        const xmax_event& e
    ) noexcept;
};
} // namespace xmax::detail
