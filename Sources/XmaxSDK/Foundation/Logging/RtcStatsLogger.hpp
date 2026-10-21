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
 * 将 RTC 指标、质量变化和启动耗时转换为可筛选的诊断记录。
 */
struct RtcStatsLogger {

    /**
     * 生成保留原始 RTC 错误码且经过凭证脱敏的诊断文本。
     *
     * @param[in] code 原生 RTC SDK 返回的错误码，诊断中保留其数值。
     * @param[in] description 原生错误描述；为空指针时使用缺省诊断。
     */
    static std::string error_message(
        int code,
        const char* description
    );

    /**
     * 记录指定方向的采集、编解码和发送或渲染指标，保留不可用值。
     *
     * @param[in] event 包含方向及视频尺寸、帧率和码率的统计事件，负数指标表示不可用。
     * @param[in] capture_fps 实际采集帧率，负数表示不可用。
     * @param[in] codec_fps 实际编码或解码帧率，负数表示不可用。
     */
    static void video(
        const xmax_event& event,
        int capture_fps = -1,
        int codec_fps = -1
    ) noexcept;

    /**
     * 按配置语言记录启动耗时、网络质量、画质调整与性能事件。
     *
     * @param[in] e 需要记录的启动时序、网络质量或性能事件。
     */
    static void event(
        const xmax_event& e
    ) noexcept;
};
} // namespace xmax::detail
