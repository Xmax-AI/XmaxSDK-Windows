//
//  XmaxRealtimeGenerationManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/StreamController.hpp"
#include "Media/Interaction/InteractionController.hpp"
#include "Service/Realtime/RealtimeContext.hpp"
#include <optional>

namespace xmax::detail {

/**
 * 管理生成条件缓存、结果确认和当前任务的轨迹发送。
 */
class XmaxRealtimeGenerationManager {

  public:

    /**
     * 生成条件的服务层值类型。
     */
    using Context = RealtimeContext;

    /**
     * 最近成功提交的生成条件；首次调用前为空。
     */
    std::optional<Context> context;

    /**
     * 负责映射和合并采样的线程安全交互控制器。
     */
    InteractionController interaction;

    /**
     * 启动生成并等待真实首帧，或更新当前任务的条件；成功后才缓存条件。
     *
     * @param[in,out] stream 当前连接的实时传输控制器。
     * @param[in] value 本次生成条件。
     * @param[in] updating 是否仅更新当前任务的条件。
     * @param[in] cancel 协作式取消判断。
     * @param[in] maintain 等待期间执行的会话保活。
     */
    void run(
        StreamController& stream,
        const Context& value,
        bool updating,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        if (updating)
            stream.update(value.prompt, value.reference, value.face, cancel, maintain);
        else {
            interaction.clear();
            stream.start(value.prompt, value.reference, value.face, cancel, maintain);
            stream.wait_remote(cancel, maintain);
        }
        require(!cancel(), XMAX_CANCELLED, "Generation cancelled");
        context = value;
    }

    /**
     * 只向同一连接批次和同一生成任务发送采样。
     *
     * @param[in,out] stream 当前连接的实时传输控制器。
     * @param[in] sample 提交时复制的采样。
     * @param[in] epoch 当前连接取消批次。
     * @param[in] cancel 协作式取消判断。
     * @param[in] maintain 等待期间执行的会话保活。
     */
    void send_interaction(
        StreamController& stream,
        const InteractionController::Sample& sample,
        uint64_t epoch,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        if (sample.epoch == epoch && sample.task == stream.task() && !cancel())
            stream.tracks(sample.points, cancel, maintain);
    }

    /**
     * 停用轨迹，按需同时清除缓存条件。
     *
     * @param[in] clear_context 是否丢弃生成条件。
     */
    void reset(
        bool clear_context
    ) {
        interaction.clear();
        if (clear_context)
            context.reset();
    }
};
} // namespace xmax::detail
