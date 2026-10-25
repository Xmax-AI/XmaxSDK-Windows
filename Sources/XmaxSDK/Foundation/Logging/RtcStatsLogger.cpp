//
//  RtcStatsLogger.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Logging/RtcStatsLogger.hpp"

namespace xmax::detail {
namespace {

/**
 * Adds units to available metrics and uses a dash for unavailable values.
 *
 * @param[in] value The metric to format; negative means unavailable.
 * @param[in] unit The unit suffix for an available value.
 */
std::string metric(
    int value,
    const char* unit
) {
    return value < 0 ? "—" : std::to_string(value) + unit;
}

/**
 * Formats a millisecond duration, using a dash for an unavailable negative value.
 *
 * @param[in] value The duration in milliseconds; negative means unavailable.
 */
std::string milliseconds(
    double value
) {
    return value < 0 ? "—" : std::to_string(value) + " ms";
}
} // namespace

/**
 * Builds a credential-redacted diagnostic preserving the native RTC error code.
 *
 * @param[in] code The native RTC error code, preserved numerically in diagnostics.
 * @param[in] description The native error description; a null pointer uses the default diagnostic.
 */
std::string RtcStatsLogger::error_message(
    int code,
    const char* description
) {
    return "Agora RTC error (" + std::to_string(code) +
           "): " + XmaxLogger::redact(description ? description : "No description");
}

/**
 * Logs directional capture, codec, and send or render metrics while preserving unavailable values.
 *
 * @param[in] e The SDK event containing direction and available video metrics.
 * @param[in] capture_fps The actual capture frame rate; negative if unavailable.
 * @param[in] codec_fps The actual encoding or decoding frame rate; negative if unavailable.
 */
void RtcStatsLogger::video(
    const xmax_event& e,
    int capture_fps,
    int codec_fps
) noexcept {
    XmaxLogger::write(
        LogCategory::RTC,
        XMAX_LOG_DEBUG,
        [&](auto language) {
            const auto label = [&](const char* cn, const char* en) {
                return XmaxLogger::localized(
                    language,
                    cn,
                    en
                );
            };
            auto result = e.direction
                              ? label(
                                  "视频下行统计 (Video Downlink Statistics)",
                                  "Video Downlink Statistics"
                              )
                              : label(
                                  "视频上行统计 (Video Uplink Statistics)",
                                  "Video Uplink Statistics"
                              );
            result +=
                "\n├─ " + label(
                    "分辨率：",
                    "Resolution: "
                ) +
                (e.width < 0 || e.height < 0 ? "—"
                                             : std::to_string(e.width) + " × " + std::to_string(e.height));
            result += "\n├─ " + label(
                "码率：",
                "Bitrate: "
            ) + metric(
                e.bitrate_kbps,
                " kbps"
            );

            if (!e.direction)
                result += "\n├─ " + label(
                    "采集帧率：",
                    "Capture Frame Rate: "
                ) + metric(
                    capture_fps,
                    " fps"
                );

            result += "\n├─ " +
                      (e.direction ? label(
                          "解码帧率：",
                          "Decode Frame Rate: "
                      )
                                   : label(
                                       "编码帧率：",
                                       "Encode Frame Rate: "
                                   )) +
                      metric(
                          codec_fps,
                          " fps"
                      );
            result += "\n├─ " +
                      (e.direction ? label(
                          "渲染帧率：",
                          "Render Frame Rate: "
                      )
                                   : label(
                                       "发送帧率：",
                                       "Send Frame Rate: "
                                   )) +
                      metric(
                          e.fps,
                          " fps"
                      );
            result += "\n" + std::string(e.direction ? "├─ " : "└─ ") +
                      label(
                          "视频丢包率：",
                          "Video Packet Loss: "
                      ) +
                      metric(
                          e.packet_loss_percent,
                          "%"
                      );

            if (e.direction)
                result += "\n└─ " + label(
                    "端到端时延：",
                    "End-to-End Delay: "
                ) + metric(
                    e.e2e_ms,
                    " ms"
                );

            return result;
        },
        XMAX_LOG_PERFORMANCE
    );
}

/**
 * Logs startup timing, network quality, quality adjustments, and performance events in the configured
 * language.
 *
 * @param[in] e The startup timing, network quality, or performance event to log.
 */
void RtcStatsLogger::event(
    const xmax_event& e
) noexcept {
    if (e.type == XMAX_EVENT_STATISTICS) {
        if (e.quality < 0)
            return; // Full video metrics are emitted by the native RTC adapter.
        XmaxLogger::write(
            LogCategory::RTC,
            XMAX_LOG_DEBUG,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "网络质量 (Network Quality)",
                    "Network Quality"
                ) +
                       "\n├─ " + XmaxLogger::localized(
                           language,
                           "方向：",
                           "Direction: "
                       ) +
                       (e.direction ? "downlink" : "uplink") + "\n└─ " +
                       XmaxLogger::localized(
                           language,
                           "等级：",
                           "Quality: "
                       ) +
                       metric(
                           e.quality,
                           ""
                       );
            },
            XMAX_LOG_PERFORMANCE
        );
    } else if (e.type == XMAX_EVENT_PERFORMANCE || e.type == XMAX_EVENT_QUALITY_CHANGE) {
        XmaxLogger::write(
            LogCategory::Stream,
            e.type == XMAX_EVENT_PERFORMANCE && e.code ? XMAX_LOG_WARNING : XMAX_LOG_INFO,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "画质 / 设备性能 (Quality / Device Performance)",
                    "Quality / Device Performance"
                ) +
                       "\n├─ direction=" + std::to_string(e.direction) + ", tier=" + metric(
                           e.quality,
                           ""
                       ) +
                       ", alarm=" + std::to_string(e.code) + "\n└─ " + std::to_string(e.width) + " × " +
                       std::to_string(e.height) + " @ " + metric(
                           e.fps,
                           " fps"
                       );
            },
            XMAX_LOG_PERFORMANCE
        );
    } else if (e.type == XMAX_EVENT_TIMING) {
        XmaxLogger::write(
            LogCategory::Realtime,
            XMAX_LOG_INFO,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "实时启动耗时 (Realtime Launch Timing)",
                    "Realtime Launch Timing"
                ) +
                       "\n├─ " + XmaxLogger::localized(
                           language,
                           "创建会话：",
                           "Create session: "
                       ) +
                       milliseconds(e.session_ms) + "\n├─ " +
                       XmaxLogger::localized(
                           language,
                           "RTC 入房：",
                           "RTC join: "
                       ) +
                       milliseconds(e.rtc_join_ms) +
                       "\n├─ " + XmaxLogger::localized(
                           language,
                           "本地推流：",
                           "Publish local stream: "
                       ) +
                       milliseconds(e.publish_ms) + "\n├─ " +
                       XmaxLogger::localized(
                           language,
                           "首帧：",
                           "First frame: "
                       ) +
                       milliseconds(e.first_frame_ms) + "\n└─ " +
                       XmaxLogger::localized(
                           language,
                           "总启动耗时：",
                           "Total startup: "
                       ) +
                       milliseconds(e.total_ms);
            }
        );
    }
}
} // namespace xmax::detail
