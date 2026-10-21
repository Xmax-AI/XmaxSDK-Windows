//
//  ExternalVideoController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/External/ExternalVideoController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {

/**
 * 校验并绑定输入规格，重置当前绑定的时间戳状态。
 *
 * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
 */
void ExternalVideoController::prepare(
    xmax_video_format format
) {
    validate_format(format);
    require(
        format.width >= 4 && format.height >= 4, XMAX_INVALID_ARGUMENT,
        "External input must be at least 4 by 4 pixels"
    );

    std::lock_guard<std::mutex> lock(mutex_);
    require(!requesting_, XMAX_INVALID_STATE, "Stop delivery before replacing an external source");
    format_ = format;
    last_timestamp_ = -1;
    XmaxLogger::write(LogCategory::Media, XMAX_LOG_INFO, [&](auto language) {
        return XmaxLogger::localized(
                   language, "外部视频已绑定 (External Video Bound)", "External Video Bound"
               ) +
               "\n└─ " + std::to_string(format.width) + " × " + std::to_string(format.height) + " @ " +
               std::to_string(format.fps) + " fps";
    });
}

/**
 * 安装发布接收器并开始请求输入帧。
 *
 * @param[in] sink 接收视频帧的同步回调，在提交帧的媒体线程调用。
 */
void ExternalVideoController::start(
    VideoFrameSink sink
) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(format_.width != 0, XMAX_INVALID_STATE, "No external input is bound");
    require(bool(sink), XMAX_INVALID_ARGUMENT, "An external frame sink is required");
    sink_ = std::move(sink);
    requesting_ = true;
}

/**
 * 停止发布交付，保留输入规格和本地预览。
 */
void ExternalVideoController::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    requesting_ = false;
    sink_ = {};
}

/**
 * 清除输入规格、发布状态和时间戳，释放当前输入绑定。
 */
void ExternalVideoController::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    requesting_ = false;
    sink_ = {};
    format_ = {};
    last_timestamp_ = -1;
}

/**
 * 检查是否已绑定有效的外部输入规格。
 */
bool ExternalVideoController::bound() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_.width != 0;
}

/**
 * 在绑定锁保护下读取当前输入规格。
 *
 * @return 当前绑定的视频输入规格，未绑定时宽高和帧率为 0。
 */
xmax_video_format ExternalVideoController::format() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_;
}

/**
 * 校验并复制借用的外部帧，同步交付给推流和本地预览接收器。
 *
 * @param[in] source 待读取的视频帧或借用视图，需包含有效的像素布局。
 */
void ExternalVideoController::push(
    const xmax_video_frame& source
) {
    // 停止操作使用同一把锁，确保返回前已完成正在执行的帧交付。
    std::lock_guard<std::mutex> lock(mutex_);
    require(format_.width != 0, XMAX_INVALID_STATE, "No external input is bound");
    require(
        source.width == format_.width && source.height == format_.height, XMAX_INVALID_ARGUMENT,
        "External frame does not match the negotiated format"
    );
    require(
        source.timestamp_us > last_timestamp_, XMAX_INVALID_ARGUMENT,
        "External frame timestamps must increase strictly"
    );

    auto frame = Frame::copy(source);
    last_timestamp_ = source.timestamp_us;

    if (requesting_ && sink_)
        sink_(frame);

    if (preview_)
        preview_(std::move(frame));
}

/**
 * 替换本地预览接收器，回调在提交帧的媒体线程执行。
 *
 * @param[in] sink 接收视频帧的同步回调，在提交帧的媒体线程调用。
 */
void ExternalVideoController::preview(
    std::function<void(Frame)> sink
) {
    std::lock_guard<std::mutex> lock(mutex_);
    preview_ = std::move(sink);
}

/**
 * 按实际采集尺寸交付摄像头帧，由 RTC 根据编码目标处理尺寸和比例。
 *
 * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
 */
void ExternalVideoController::push_owned(
    Frame frame
) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(format_.width != 0, XMAX_INVALID_STATE, "No camera input is bound");
    validate_frame(frame.view());
    require(frame.timestamp_us > last_timestamp_, XMAX_INVALID_ARGUMENT, "Capture timestamps must increase");
    last_timestamp_ = frame.timestamp_us;

    if (requesting_ && sink_)
        sink_(frame);

    if (preview_)
        preview_(std::move(frame));
}
} // namespace xmax::detail
