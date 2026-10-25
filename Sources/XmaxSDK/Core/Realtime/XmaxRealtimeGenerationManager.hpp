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
 * Manages cached generation conditions, result confirmation, and task trajectory delivery.
 */
class XmaxRealtimeGenerationManager {

  public:

    /**
     * The service-layer generation-conditions type.
     */
    using Context = RealtimeContext;

    /**
     * The most recently committed generation conditions; empty before the first submission.
     */
    std::optional<Context> context;

    /**
     * The thread-safe controller for coordinate mapping and sample coalescing.
     */
    InteractionController interaction;

    /**
     * Starts generation and waits for a real first frame, or updates current conditions; caches conditions
     * only on success.
     *
     * @param[in,out] stream The current connection's realtime transport controller.
     * @param[in] value The conditions for this generation.
     * @param[in] updating Whether to update only the current task's conditions.
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] maintain The session keepalive callback invoked while waiting.
     */
    void run(
        StreamController& stream,
        const Context& value,
        bool updating,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        if (updating)
            stream.update(
                value.prompt,
                value.reference,
                value.face,
                cancel,
                maintain
            );
        else {
            interaction.clear();
            stream.start(
                value.prompt,
                value.reference,
                value.face,
                cancel,
                maintain
            );
            stream.wait_remote(
                cancel,
                maintain
            );
        }

        require(
            !cancel(),
            XMAX_CANCELLED,
            "Generation cancelled"
        );
        context = value;
    }

    /**
     * Sends samples only to the same connection revision and generation task.
     *
     * @param[in,out] stream The current connection's realtime transport controller.
     * @param[in] sample The sample copied at submission.
     * @param[in] epoch The current connection's cancellation revision.
     * @param[in] cancel The cooperative cancellation predicate.
     * @param[in] maintain The session keepalive callback invoked while waiting.
     */
    void send_interaction(
        StreamController& stream,
        const InteractionController::Sample& sample,
        uint64_t epoch,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        if (sample.epoch == epoch && sample.task == stream.task() && !cancel())
            stream.tracks(
                sample.points,
                cancel,
                maintain
            );
    }

    /**
     * Disables trajectory delivery and optionally clears cached conditions.
     *
     * @param[in] clear_context Whether to discard generation conditions.
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
