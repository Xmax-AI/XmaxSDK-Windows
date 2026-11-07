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
     * Retains the media controller's interaction channel for generation boundaries.
     *
     * @param[in,out] interaction The local interaction channel, which outlives this manager.
     */
    explicit XmaxRealtimeGenerationManager(
        InteractionController& interaction
    ) : interaction_(interaction) {
    }

    /**
     * Starts generation and waits for result publication, or updates current conditions; caches conditions
     * only on success.
     *
     * @param[in,out] stream The current connection's realtime transport controller.
     * @param[in] value The conditions for this generation.
     * @param[in] updating Whether to update only the current task's conditions.
     * @param[in] cancel The cooperative cancellation predicate.
     */
    void run(
        StreamController& stream,
        const Context& value,
        bool updating,
        const Cancel& cancel
    ) {
        if (updating)
            stream.update(
                value.prompt,
                value.reference,
                value.face,
                cancel
            );
        else {
            interaction_.clear();
            const auto deadline = Clock::now() + std::chrono::seconds(30);
            stream.start(
                value.prompt,
                value.reference,
                value.face,
                cancel,
                deadline
            );
            stream.wait_remote(cancel, deadline);
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
     */
    void send_interaction(
        StreamController& stream,
        const InteractionController::Sample& sample,
        uint64_t epoch,
        const Cancel& cancel
    ) {
        if (sample.epoch == epoch && sample.task == stream.task() && !cancel())
            stream.tracks(
                sample.points,
                cancel
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
        interaction_.clear();
        if (clear_context)
            context.reset();
    }

  private:

    /**
     * The borrowed local interaction channel owned by the media controller.
     */
    InteractionController& interaction_;
};
} // namespace xmax::detail
