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

namespace xmax::detail {

/**
 * 向实时控制器交付 RTC 帧、事件、错误和首帧发布通知。
 */
struct RtcCallbacks {

    /**
     * 交付独立拥有的视频帧的回调，在原生媒体线程调用。
     */
    std::function<void(Frame)> frame;

    /**
     * 交付 RTC 状态、统计和网络质量事件的回调。
     */
    std::function<void(xmax_event)> event;

    /**
     * 接收原生 RTC 失败原因的回调，在原生 RTC 线程调用。
     */
    std::function<void(Error)> failure;

    /**
     * 确认本地视频首次实际发布的通知，区别于单次推帧返回成功。
     */
    std::function<void()> published;
};
} // namespace xmax::detail
