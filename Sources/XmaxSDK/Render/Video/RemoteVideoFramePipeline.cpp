//
//  RemoteVideoFramePipeline.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Video/RemoteVideoFramePipeline.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {

/**
 * 启用指定批次的帧交付，并丢弃先前待处理帧。
 *
 * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
 */
void RemoteVideoFramePipeline::activate(
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (token < token_)
        return;

    token_ = token;
    latest_.reset();
    active_ = true;
    XmaxLogger::write(LogCategory::Render, XMAX_LOG_DEBUG, [](auto language) {
        return XmaxLogger::localized(
            language, "预览绑定已激活 (Preview Binding Activated)", "Preview Binding Activated"
        );
    });
}

/**
 * 停用帧交付，并丢弃待处理帧。
 *
 * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
 */
void RemoteVideoFramePipeline::deactivate(
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_)
        return;
    token_ = token;
    active_ = false;
    latest_.reset();
}

/**
 * 仅接收属于当前批次的视频帧，并替换缓冲区中的旧帧。
 *
 * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
 * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
 */
void RemoteVideoFramePipeline::submit(
    Frame frame,
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && token == token_)
        latest_ = std::make_shared<Frame>(std::move(frame));
}

/**
 * 非阻塞取出最新帧，并清空帧缓冲区。
 *
 * @return 独立拥有的最新帧；当前无待处理帧时为 nullptr。
 */
std::shared_ptr<Frame> RemoteVideoFramePipeline::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(latest_, {});
}
} // namespace xmax::detail
