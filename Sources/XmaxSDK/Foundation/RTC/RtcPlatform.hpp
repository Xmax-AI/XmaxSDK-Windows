//
//  RtcPlatform.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/RTC/RtcCallbacks.hpp"
#include "Foundation/RTC/VideoEncodingConfiguration.hpp"
#include "Foundation/RTC/Connection.hpp"
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

/**
 * 定义房间连接、外部视频发布、结果接收与编码配置的传输契约。
 */
class Rtc {

  public:

    /**
     * 在离开房间并排空回调后释放 RTC 适配器。
     *
     * @note 销毁前应结束使用本对象的并发调用。
     */
    virtual ~Rtc() = default;

    /**
     * 使用编码参数、音频策略和回调初始化原生 RTC，并等待加入房间。
     *
     * @param[in] connection 当前 RTC 房间连接信息。
     * @param[in] encoding 已解析的编码尺寸、帧率、码率和降级策略。
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     * @param[in] callbacks 接收结果帧、RTC 事件、错误和首次发布通知的回调；停止后不再触发。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    virtual void join(
        const Connection& connection,
        VideoEncodingConfiguration encoding,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel
    ) = 0;

    /**
     * 发送一个编码后的房间信令分片，失败时报告传输错误。
     *
     * @param[in] bytes 待处理的编码字节，本次同步调用返回前必须有效。
     */
    virtual void send(
        const std::string& bytes
    ) = 0;

    /**
     * 发布一帧 SDK 拥有的输入像素，允许从媒体线程调用。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     */
    virtual void push(
        const Frame& frame
    ) = 0;

    /**
     * 在开始新生成任务前重置结果首帧跟踪状态。
     */
    virtual void begin_generation() = 0;

    /**
     * 等待真实结果首帧，同时响应取消并执行周期保活。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] keepalive 等待期间周期执行的保活函数，不得持有跨线程生命周期锁。
     */
    virtual void wait_remote(
        const Cancel& cancel,
        const std::function<void()>& keepalive
    ) = 0;

    /**
     * 应用范围为 [0, 1] 的远端播放音量。
     *
     * @param[in] value 范围为 [0, 1] 的有限音量值，0 表示静音。
     */
    virtual void volume(
        float value
    ) = 0;

    /**
     * 应用生效的视频尺寸、帧率和码率策略。
     *
     * @param[in] value 已解析的编码尺寸、帧率、码率限制与降级偏好。
     */
    virtual void configure_encoding(
        VideoEncodingConfiguration value
    ) = 0;

    /**
     * 离开 RTC 并在返回前排空内部帧、事件和错误回调。
     */
    virtual void leave() noexcept = 0;
};

/**
 * 创建共享原生 RTC 适配器的工厂回调，不得返回空对象。
 */
using RtcFactory = std::function<std::shared_ptr<Rtc>()>;

/**
 * 检查当前构建是否包含原生 RTC 后端。
 */
bool rtc_available() noexcept;

/**
 * 创建原生 RTC 适配器，不支持的平台返回明确的能力错误。
 *
 * @return 可共享的原生 RTC 适配器；平台不支持时抛出能力错误。
 */
std::shared_ptr<Rtc> make_rtc();
} // namespace xmax::detail
