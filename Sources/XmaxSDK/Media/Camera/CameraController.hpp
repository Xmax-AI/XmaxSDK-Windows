//
//  CameraController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/Camera/CameraCapture.hpp"
#include <condition_variable>
#include <mutex>

namespace xmax::detail {

/**
 * 启动原生采集，保持帧的实际尺寸和像素格式，等待真实首帧后完成准备。
 */
class CameraController {

  public:

    /**
     * 保存用于摄像头枚举与采集的原生适配器工厂。
     */
    explicit CameraController(
        CameraFactory factory = make_camera_capture
    )
        : factory_(std::move(factory)) {
    }

    /**
     * 停止摄像头采集并排空回调，然后释放拥有的媒体资源。
     */
    ~CameraController() {
        stop();
    }

    /**
     * 枚举摄像头标识与名称，不启动采集。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     */
    std::vector<CameraDevice> devices() {
        return factory_()->devices();
    }

    /**
     * 启动原生采集并直接交付原始尺寸的视频帧，等待首帧、错误或取消。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] format 请求的视频像素宽高及每秒帧数，须符合调用接口的规格限制。
     * @param[in] output 接收原始采集帧和采集错误的回调，在原生媒体线程同步触发。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void
    start(
        const std::string& device,
        xmax_video_format format,
        CaptureCallbacks output,
        const Cancel& cancel
    );

    /**
     * 停止摄像头采集，并在返回前排空帧与错误回调。
     */
    void stop() noexcept;

  private:

    /**
     * 创建原生摄像头适配器的工厂，用于枚举与采集。
     */
    CameraFactory factory_;

    /**
     * 当前独占的原生摄像头适配器。
     */
    std::unique_ptr<CameraCapture> capture_;

    /**
     * 保护采集就绪标记、失败原因和最近时间戳的互斥锁。
     */
    std::mutex mutex_;

    /**
     * 通知首帧就绪或采集失败的条件变量。
     */
    std::condition_variable changed_;

    /**
     * 当前采集绑定是否已收到并交付有效首帧。
     */
    bool ready_ = false;

    /**
     * 最近一次采集或帧转换失败原因，供启动等待者读取。
     */
    std::string failure_;

    /**
     * 当前输入绑定最近一次已接收的单调微秒时间戳。
     */
    int64_t last_timestamp_ = -1;
};
} // namespace xmax::detail
