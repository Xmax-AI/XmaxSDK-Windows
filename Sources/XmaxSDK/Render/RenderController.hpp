//
//  RenderController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Render/Video/RemoteVideoFramePipeline.hpp"
#include "Render/Trajectory/TrajectoryRegistry.hpp"
#include "Render/Trajectory/DefaultTrajectoryEffectRenderer.hpp"

namespace xmax::detail {

/**
 * 分别缓存最新的本地预览帧和远端结果帧，丢弃已失效会话或媒体绑定的回调。
 */
class RenderController {

  public:

    /**
     * 当前任务轨迹缓存，供默认效果和自定义绘制共享。
     */
    TrajectoryRegistry trajectory;

    /**
     * 为指定连接批次启用远端结果帧交付。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void bind(
        uint64_t token
    ) {
        remote_.activate(token);
    }

    /**
     * 停用远端结果帧交付，并清除待处理帧。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void unbind(
        uint64_t token
    ) {
        remote_.deactivate(token);
    }

    /**
     * 仅在连接批次仍有效时接收远端结果帧。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void receive(
        Frame frame,
        uint64_t token
    ) {
        remote_.submit(std::move(frame), token);
    }

    /**
     * 非阻塞取出最新的远端结果帧，并转移该帧的所有权。
     *
     * @return 返回的帧持有像素数据；没有新帧时返回空对象。
     */
    std::shared_ptr<Frame> acquire_frame() {
        return remote_.acquire();
    }

    /**
     * 为指定媒体批次启用本地预览交付。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void bind_local(
        uint64_t token
    ) {
        local_.activate(token);
    }

    /**
     * 停用本地预览交付，并清除待处理帧。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void unbind_local(
        uint64_t token
    ) {
        local_.deactivate(token);
    }

    /**
     * 仅在媒体批次仍有效时接收本地预览帧。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     */
    void receive_local(
        Frame frame,
        uint64_t token
    ) {
        local_.submit(std::move(frame), token);
    }

    /**
     * 非阻塞取出最新的本地预览帧，并转移该帧的所有权。
     *
     * @return 返回的帧持有像素数据；没有新帧时返回空对象。
     */
    std::shared_ptr<Frame> acquire_local_frame() {
        return local_.acquire();
    }

    /**
     * 在原生窗口显示结果帧，并绘制当前任务的默认轨迹。
     *
     * @param[in] frame 当前显示的结果帧。
     * @param[in] window 当前线程拥有的 HWND。
     * @param[in] content_mode 视频与轨迹共同使用的布局方式。
     * @param[in] interaction_enabled 当前生成任务是否允许显示交互效果。
     */
    void render_window(
        const Frame& frame,
        void* window,
        int32_t content_mode,
        bool interaction_enabled
    ) const {
        xmax::detail::render_window(frame, window, content_mode);
        if (interaction_enabled)
            trajectory_renderer_.render(
                window, {frame.width, frame.height, 30}, content_mode, trajectory.snapshot()
            );
    }

  private:

    /**
     * 原生窗口默认轨迹效果的呈现组件。
     */
    DefaultTrajectoryEffectRenderer trajectory_renderer_;

    /**
     * 保存最新远端生成结果帧的邮箱。
     */
    RemoteVideoFramePipeline remote_;

    /**
     * 保存最新本地预览帧的邮箱。
     */
    RemoteVideoFramePipeline local_;
};
} // namespace xmax::detail
