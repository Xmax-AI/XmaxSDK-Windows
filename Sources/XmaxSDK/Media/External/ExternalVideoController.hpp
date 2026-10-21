//
//  ExternalVideoController.hpp
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
 * 同步接收视频帧；回调返回后仍需使用像素时，接收方必须自行复制。
 */
using VideoFrameSink = std::function<void(const Frame&)>;

/**
 * 管理外部输入绑定、严格递增的时间戳以及推流和预览交付。
 */
class ExternalVideoController {

  public:

    /**
     * 校验并绑定输入规格，重置当前绑定的时间戳状态。
     *
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     */
    void prepare(
        xmax_video_format format
    );

    /**
     * 安装发布接收器并开始请求输入帧。
     *
     * @param[in] sink 接收视频帧的同步回调，在提交帧的媒体线程调用。
     */
    void start(
        VideoFrameSink sink
    );

    /**
     * 停止发布交付，保留输入规格和本地预览。
     */
    void stop();

    /**
     * 清除输入规格、发布状态和时间戳，释放当前输入绑定。
     */
    void close();

    /**
     * 校验并复制借用的外部帧，同步交付给推流和本地预览接收器。
     *
     * @param[in] source 待读取的视频帧或借用视图，需包含有效的像素布局。
     */
    void push(
        const xmax_video_frame& source
    );

    /**
     * 按实际采集尺寸交付摄像头帧，由 RTC 根据编码目标处理尺寸和比例。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     */
    void push_owned(
        Frame frame
    );

    /**
     * 检查是否已绑定有效的外部输入规格。
     */
    bool bound() const;

    /**
     * 非阻塞检查当前是否需要向发布接收器供帧。
     */
    bool requesting() const {
        return requesting_.load();
    }

    /**
     * 在绑定锁保护下读取当前输入规格。
     *
     * @return 当前绑定的视频输入规格，未绑定时宽高和帧率为 0。
     */
    xmax_video_format format() const;

    /**
     * 替换本地预览接收器，回调在提交帧的媒体线程执行。
     *
     * @param[in] sink 接收视频帧的同步回调，在提交帧的媒体线程调用。
     */
    void preview(
        std::function<void(Frame)> sink
    );

  private:

    /**
     * 保护输入绑定、时间戳及帧接收器，并协调停止与交付的互斥锁。
     */
    mutable std::mutex mutex_;

    /**
     * 外部输入规格或摄像头编码目标，不限制摄像头帧的实际采集尺寸。
     */
    xmax_video_format format_{};

    /**
     * 当前输入绑定最近一次已接收的单调微秒时间戳。
     */
    int64_t last_timestamp_ = -1;

    /**
     * 当前输入是否正在向活动 RTC 接收器交付帧。
     */
    std::atomic<bool> requesting_{false};

    /**
     * 当前发布帧接收器，在提交帧的媒体线程同步执行。
     */
    VideoFrameSink sink_;

    /**
     * 当前本地预览接收器，在提交帧的媒体线程同步执行。
     */
    std::function<void(Frame)> preview_;
};
} // namespace xmax::detail
