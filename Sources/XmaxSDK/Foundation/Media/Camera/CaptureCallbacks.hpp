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
 * Receives owned capture frames and asynchronous failures on the capture thread.
 */
struct CaptureCallbacks {

    /**
     * The owned-frame callback, invoked on the native media thread.
     */
    std::function<void(Frame)> frame;

    /**
     * The native capture failure receiver, invoked on the media thread.
     */
    std::function<void(Error)> failure;
};
} // namespace xmax::detail
