//
//  CaptureCallbacks.hpp
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
 * 接收具有独立所有权的采集帧和异步采集错误；回调运行于采集线程。
 */
struct CaptureCallbacks {

    /**
     * 交付独立拥有的视频帧的回调，在原生媒体线程调用。
     */
    std::function<void(Frame)> frame;

    /**
     * 接收原生采集失败原因的回调，在原生媒体线程调用。
     */
    std::function<void(Error)> failure;
};
} // namespace xmax::detail
