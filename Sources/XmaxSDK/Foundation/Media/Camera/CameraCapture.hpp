//
//  CameraCapture.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/Camera/CaptureCallbacks.hpp"
#include "Foundation/Media/Camera/CameraDevice.hpp"
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

/**
 * 定义原生摄像头枚举、启动和停止契约；停止返回前必须排空回调。
 */
class CameraCapture {

  public:

    /**
     * 在停止采集并排空回调后释放平台适配器。
     *
     * @note 销毁前应结束使用本对象的并发调用。
     */
    virtual ~CameraCapture() = default;

    /**
     * 枚举设备标识、显示名称和默认设备，不启动采集。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     */
    virtual std::vector<CameraDevice> devices() = 0;

    /**
     * 启动支持取消的原生采集，通过回调交付具有独立所有权的视频帧。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] format 期望的编码尺寸和采集帧率；实际采集尺寸以帧回调为准。
     * @param[in] callbacks 采集帧与失败回调，在原生媒体线程交付，停止返回前排空。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    virtual void start(
        const std::string& device,
        xmax_video_format format,
        CaptureCallbacks callbacks,
        const Cancel& cancel
    ) = 0;

    /**
     * 停止原生采集，返回前排空全部帧和错误回调。
     */
    virtual void stop() noexcept = 0;
};

/**
 * 创建独占原生摄像头适配器的工厂回调，不得返回空对象。
 */
using CameraFactory = std::function<std::unique_ptr<CameraCapture>()>;

/**
 * 创建原生摄像头适配器，不支持的平台返回明确的能力错误。
 *
 * @return 独占拥有的原生摄像头适配器；平台不支持时抛出能力错误。
 */
std::unique_ptr<CameraCapture> make_camera_capture();

/**
 * 检查当前 SDK 构建是否提供原生摄像头采集后端。
 */
bool camera_available() noexcept;
} // namespace xmax::detail
