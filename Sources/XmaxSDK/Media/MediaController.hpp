//
//  MediaController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Media/Camera/CameraController.hpp"
#include "Media/External/ExternalVideoController.hpp"

namespace xmax::detail {

/**
 * 管理 SDK 摄像头与外部输入的互斥绑定，并保留统一的帧交付通道。
 */
class MediaController {

  public:

    /**
     * 使用指定摄像头工厂组合媒体采集与外部帧交付通道。
     */
    explicit MediaController(
        CameraFactory factory = make_camera_capture
    )
        : camera_(std::move(factory)) {
    }

    /**
     * 停止摄像头回调并清除外部输入绑定，释放本地媒体资源。
     */
    ~MediaController() {
        close();
    }

    /**
     * 借用统一的外部帧交付控制器，不转移所有权。
     *
     * @return 当前对象内部的帧交付控制器；当前对象销毁后不可再访问。
     */
    ExternalVideoController& external() {
        return external_;
    }

    /**
     * 借用统一的外部帧交付控制器，不转移所有权。
     *
     * @return 当前对象内部的帧交付控制器；当前对象销毁后不可再访问。
     */
    const ExternalVideoController& external() const {
        return external_;
    }

    /**
     * 停止发布交付，保留已绑定媒体和本地预览。
     */
    void stop() {
        external_.stop();
    }

    /**
     * 停止采集并清除输入绑定和设备状态。
     */
    void close() {
        camera_.stop();
        external_.close();
        camera_active_ = false;
        device_.clear();
    }

    /**
     * 替换当前媒体源并绑定支持的外部输入规格。
     *
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     */
    void prepare_external(
        xmax_video_format format
    ) {
        close();
        external_.prepare(format);
    }

    /**
     * 替换当前媒体源并启动 SDK 摄像头采集，等待首帧就绪。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     * @param[in] failure 接收异步失败原因的回调；停止操作返回前应结束其交付。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void prepare_camera(
        const std::string& device,
        xmax_video_format format,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        close();
        external_.prepare(format);
        camera_active_ = true;
        device_ = device;

        try {
            camera_.start(
                device, format,
                {[this](Frame frame) { external_.push_owned(std::move(frame)); }, std::move(failure)}, cancel
            );
        } catch (...) {
            close();
            throw;
        }
    }

    /**
     * 使用当前输入规格切换摄像头采集。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] failure 接收异步失败原因的回调；停止操作返回前应结束其交付。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void switch_camera(
        const std::string& device,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        require(camera_active_, XMAX_INVALID_STATE, "No camera stream is active");
        camera_.stop();
        external_.prepare(external_.format());
        camera_.start(
            device, external_.format(),
            {[this](Frame frame) { external_.push_owned(std::move(frame)); }, std::move(failure)}, cancel
        );
        device_ = device;
    }

    /**
     * 更新当前摄像头目标规格，保留设备选择和预览回调。
     *
     * @param[in] format 新的编码目标尺寸和帧率。
     * @param[in] failure 当前媒体绑定的失败回调。
     * @param[in] cancel 协作式取消判断。
     */
    void update_camera_format(
        xmax_video_format format,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        require(camera_active_, XMAX_INVALID_STATE, "No camera stream is active");
        const auto previous = external_.format();
        camera_.stop();
        external_.prepare(format);
        try {
            camera_.start(
                device_, format, {[this](Frame frame) { external_.push_owned(std::move(frame)); }, failure},
                cancel
            );
        } catch (...) {
            const auto original = std::current_exception();
            if (cancel()) {
                close();
            } else {
                try {
                    external_.prepare(previous);
                    camera_.start(
                        device_, previous,
                        {[this](Frame frame) { external_.push_owned(std::move(frame)); }, failure}, cancel
                    );
                } catch (...) {
                    close();
                }
            }
            std::rethrow_exception(original);
        }
    }

    /**
     * 检查当前是否使用 SDK 摄像头绑定。
     */
    bool camera_active() const {
        return camera_active_.load();
    }

    /**
     * 枚举摄像头设备，不启动采集。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     */
    std::vector<CameraDevice> devices() {
        return camera_.devices();
    }

  private:

    /**
     * 统一管理输入帧发布和本地预览交付的控制器。
     */
    ExternalVideoController external_;

    /**
     * 管理 SDK 原生摄像头采集和帧交付的控制器。
     */
    CameraController camera_;

    /**
     * 当前输入是否绑定到 SDK 摄像头的原子标记。
     */
    std::atomic<bool> camera_active_{false};

    /**
     * 当前摄像头的设备标识。
     */
    std::string device_;
};
} // namespace xmax::detail
