//
//  EncodingController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include "Stream/Quality/QualityController.hpp"
#include "Foundation/RTC/RtcPlatform.hpp"

namespace xmax::detail {

/**
 * Resolves bitrate and degradation settings, committing overrides only after native configuration succeeds.
 */
class EncodingController {

  public:

    /**
     * Resolves uplink encoding for a quality level.
     *
     * @param[in] baseline The baseline media encoding format.
     * @param[in] level The target quality level, in [1, 5].
     *
     * @return The level's dimensions, frame rate, bitrate limits, and degradation preference.
     */
    VideoEncodingConfiguration resolve(
        xmax_video_format baseline,
        int level
    ) const {
        const auto format = quality_format(
            baseline,
            level,
            false
        );
        const auto budget = bitrate(format);
        const int maximum = value_.maximum_bitrate_kbps == 0
                                ? budget.second
                                : value_.maximum_bitrate_kbps;
        const int minimum =
            value_.minimum_bitrate_kbps < 0 ? std::min(500, maximum) : value_.minimum_bitrate_kbps;
        require(
            minimum <= maximum,
            XMAX_INVALID_ARGUMENT,
            "Minimum bitrate exceeds maximum bitrate"
        );
        return {format, minimum, maximum, value_.preference, mirrored_};
    }

    /**
     * Validates and applies overrides, restoring previous values if native configuration fails.
     *
     * @param[in] value The bitrate limits and degradation preference.
     * @param[in] baseline The baseline encoding format; dimensions are 0 before media preparation.
     * @param[in] level The current uplink level.
     * @param[in] rtc The joined RTC backend, or null when disconnected.
     */
    void configure(
        xmax_encoding_config value,
        xmax_video_format baseline,
        int level,
        Rtc* rtc
    ) {
        require(
            value.minimum_bitrate_kbps >= -1 && value.minimum_bitrate_kbps <= 100000 &&
                value.maximum_bitrate_kbps >= 0 && value.maximum_bitrate_kbps <= 100000 &&
                (value.maximum_bitrate_kbps == 0 || value.minimum_bitrate_kbps < 0 ||
                 value.minimum_bitrate_kbps <= value.maximum_bitrate_kbps) &&
                value.preference >= XMAX_ENCODER_AUTO && value.preference <= XMAX_ENCODER_MAINTAIN_QUALITY,
            XMAX_INVALID_ARGUMENT,
            "Invalid video encoding configuration"
        );

        const auto previous = value_;
        value_ = value;
        try {
            if (rtc)
                rtc->configure_encoding(resolve(
                    baseline,
                    level
                ));
        } catch (...) {
            value_ = previous;
            throw;
        }
    }

    /**
     * Applies uplink encoding mirroring, restoring the previous value if native updates fail.
     *
     * @param[in] enabled Whether to horizontally mirror the published video.
     * @param[in] baseline The current media's baseline encoding format.
     * @param[in] level The current uplink quality level.
     * @param[in] rtc The active RTC backend, or null when disconnected.
     */
    void mirror(
        bool enabled,
        xmax_video_format baseline,
        int level,
        Rtc* rtc
    ) {
        if (mirrored_ == enabled)
            return;
        const auto previous = mirrored_;
        mirrored_ = enabled;
        try {
            if (rtc)
                rtc->configure_encoding(resolve(
                    baseline,
                    level
                ));
        } catch (...) {
            mirrored_ = previous;
            throw;
        }
    }

  private:

    /**
     * The uplink mirror setting for the current and next connection.
     */
    bool mirrored_ = false;

    /**
     * The unspecified minimum bitrate defaults to min(500, maximum bitrate), in kbps.
     */
    xmax_encoding_config value_{
        sizeof(xmax_encoding_config),
        -1,
        0,
        XMAX_ENCODER_MAINTAIN_FRAMERATE
    };
};
} // namespace xmax::detail
