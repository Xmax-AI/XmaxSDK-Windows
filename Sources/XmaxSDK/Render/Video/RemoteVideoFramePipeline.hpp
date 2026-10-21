//
//  RemoteVideoFramePipeline.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * 保存当前批次的最新一帧，读取时转移其所有权，不积压历史视频帧。
 */
class RemoteVideoFramePipeline {

  public:

    /**
     * 启用指定批次的帧交付，并丢弃先前待处理帧。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void activate(
        uint64_t token
    );

    /**
     * 停用帧交付，并丢弃待处理帧。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void deactivate(
        uint64_t token
    );

    /**
     * 仅接收属于当前批次的视频帧，并替换缓冲区中的旧帧。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void submit(
        Frame frame,
        uint64_t token
    );

    /**
     * 非阻塞取出最新帧，并清空帧缓冲区。
     *
     * @return 独立拥有的最新帧；当前无待处理帧时为 nullptr。
     */
    std::shared_ptr<Frame> acquire();

  private:

    /**
     * 保护当前帧批次、接收状态和帧缓冲区的互斥锁。
     */
    std::mutex mutex_;

    /**
     * 当前接收的视频帧所属批次。
     */
    uint64_t token_ = 0;

    /**
     * 是否接收当前批次的视频帧。
     */
    bool active_ = false;

    /**
     * 等待接入方消费的最新帧；新帧替换旧帧，消费后置空。
     */
    std::shared_ptr<Frame> latest_;
};
} // namespace xmax::detail
