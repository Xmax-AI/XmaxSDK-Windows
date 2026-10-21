//
//  XmaxRealtimeConnectionManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Realtime/RealtimeSessionService.hpp"
#include "Stream/StreamController.hpp"

namespace xmax::detail {

/**
 * 管理服务端会话与 RTC 连接的成对分配、保活和释放。
 */
class XmaxRealtimeConnectionManager {

  public:

    /**
     * 保留执行会话请求的共享 API 服务。
     *
     * @param[in] api 当前实时模型与环境的 API 服务。
     */
    explicit XmaxRealtimeConnectionManager(
        std::shared_ptr<ApiService> api
    )
        : service_(std::move(api)) {
    }

    /**
     * 先校验 RTC 后端，再分配服务端会话，避免能力不足时创建计费资源。
     *
     * @param[in,out] stream 当前实时传输控制器。
     * @param[in] cancel 协作式取消判断。
     * @return 已验证的服务端会话，生命周期由当前对象管理。
     */
    const RealtimeSession& create(
        StreamController& stream,
        const Cancel& cancel
    ) {
        stream.allocate();
        return service_.create(cancel);
    }

    /**
     * 获取当前活动会话，未分配时标识为空。
     */
    const RealtimeSession& active() const {
        return service_.active();
    }

    /**
     * 按已创建会话加入 RTC 房间。
     *
     * @param[in,out] stream 当前实时传输控制器。
     * @param[in] format 媒体编码基准。
     * @param[in] microphone 是否启用麦克风。
     * @param[in] callbacks RTC 帧与生命周期回调。
     * @param[in] cancel 协作式取消判断。
     * @param[in] canvas 模型生成画布。
     */
    void join(
        StreamController& stream,
        xmax_video_format format,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel,
        xmax_video_format canvas
    ) {
        stream.join(service_.active().room, format, microphone, std::move(callbacks), cancel, canvas);
    }

    /**
     * 同时维持服务端会话和 RTC 房间的活跃状态。
     *
     * @param[in,out] stream 当前实时传输控制器。
     * @param[in] cancel 协作式取消判断。
     */
    void heartbeat(
        StreamController& stream,
        const Cancel& cancel
    ) {
        service_.heartbeat(cancel);
        stream.heartbeat(cancel);
    }

    /**
     * 先停止 RTC 发布与回调，再释放服务端会话。
     *
     * @param[in,out] stream 当前实时传输控制器。
     */
    void close(
        StreamController& stream
    ) {
        stream.stop();
        service_.close();
    }

  private:

    /**
     * 当前模型对应的服务端会话资源。
     */
    RealtimeSessionService service_;
};
} // namespace xmax::detail
