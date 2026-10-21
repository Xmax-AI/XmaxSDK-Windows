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
 * 解析编码码率与降级偏好，并在原生配置成功后提交覆盖值。
 */
class EncodingController {

  public:

    /**
     * 解析指定档位的上行编码配置。
     *
     * @param[in] baseline 媒体编码基准规格。
     * @param[in] level 目标档位，范围为 [1, 5]。
     * @return 该档位的视频尺寸、帧率、码率限制和降级偏好。
     */
    VideoEncodingConfiguration resolve(
        xmax_video_format baseline,
        int level
    ) const {
        const auto format = quality_format(baseline, level, false);
        const auto budget = bitrate(format);
        const int maximum = value_.maximum_bitrate_kbps == 0
                                ? std::max(budget.second, std::max(0, value_.minimum_bitrate_kbps))
                                : value_.maximum_bitrate_kbps;
        const int minimum =
            value_.minimum_bitrate_kbps < 0 ? std::min(500, maximum) : value_.minimum_bitrate_kbps;
        require(minimum <= maximum, XMAX_INVALID_ARGUMENT, "Minimum bitrate exceeds maximum bitrate");
        return {format, minimum, maximum, value_.preference};
    }

    /**
     * 校验并应用覆盖配置，原生配置失败时恢复旧值。
     *
     * @param[in] value 码率限制与降级偏好。
     * @param[in] baseline 媒体编码基准规格，未准备时宽高为 0。
     * @param[in] level 当前上行档位。
     * @param[in] rtc 已加入房间的 RTC 后端，未连接时为空。
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
            XMAX_INVALID_ARGUMENT, "Invalid video encoding configuration"
        );
        const auto previous = value_;
        value_ = value;
        try {
            if (rtc)
                rtc->configure_encoding(resolve(baseline, level));
        } catch (...) {
            value_ = previous;
            throw;
        }
    }

  private:

    /**
     * 未指定最小码率时使用 min(500, 最大码率)，单位为 kbps。
     */
    xmax_encoding_config value_{sizeof(xmax_encoding_config), -1, 0, XMAX_ENCODER_MAINTAIN_FRAMERATE};
};
} // namespace xmax::detail
