//
//  XmaxRealtimeManager.Generation.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Foundation/Text/Utf8.hpp"

namespace xmax::detail {

using Lifecycle = XmaxRealtimeLifecycleManager;
using OperationKind = Lifecycle::OperationKind;
using Scope = Lifecycle::TerminationScope;

/**
 * Sends generation conditions, enables input delivery, and waits for the first result frame.
 *
 * @param[in] context The prompt, reference image, and face-restoration conditions for this generation.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 *
 * @return True when a new task reached its first result; false when existing conditions were updated.
 */
bool XmaxRealtimeManager::generate_impl(
    const Context& context,
    const Cancel& cancel
) {
    if (lifecycle_.state() == XMAX_GENERATING) {
        generation_.run(
            stream_,
            context,
            true,
            cancel
        );
        std::lock_guard<std::mutex> lock(interaction_mutex_);
        media_.interaction.clear();
        render_.trajectory.clear();
        return false;
    }

    require(
        lifecycle_.state() == XMAX_CONNECTED,
        XMAX_INVALID_STATE,
        "No realtime connection is active"
    );

    timing_.begin_signal();
    render_.trajectory.clear();
    const auto epoch = lifecycle_.epoch();
    render_.bind(epoch);
    generation_.run(
        stream_,
        context,
        false,
        cancel
    );
    render_.wait_displayable_result(epoch, cancel);
    require(
        !cancel(),
        XMAX_CANCELLED,
        "Operation cancelled"
    );
    stream_.activate_result_audio();
    timing_.finish_signal();
    return true;
}

/**
 * Connects if needed and starts generation, blocking until the first result frame.
 *
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 */
void XmaxRealtimeManager::start(
    std::string prompt,
    std::string reference,
    bool face,
    bool microphone
) {
    request_generation(
        Context{std::move(prompt), std::move(reference), face},
        microphone
    );
}

/**
 * Starts a task with supplied or cached conditions, blocking until the first result frame.
 *
 * @param[in] value Optional generation conditions; null reuses previous conditions, but the first call
 * requires them.
 */
uint64_t XmaxRealtimeManager::generate(
    const xmax_generation_context* value,
    uint64_t local_revision,
    std::optional<bool> face_restoration,
    bool connect_if_needed,
    Cancel caller_cancelled,
    bool update_only
) {
    std::optional<Context> incoming;

    if (value)
        incoming = Context{
            value->prompt,
            value->reference_path ? value->reference_path : "",
            value->face_restoration != 0
        };

    return request_generation(
        std::move(incoming),
        false,
        local_revision,
        face_restoration,
        connect_if_needed,
        std::move(caller_cancelled),
        update_only
    );
}

/**
 * Replaces the current task's generation conditions.
 *
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 */
void XmaxRealtimeManager::update(
    std::string prompt,
    std::string reference,
    bool face
) {
    request_generation(
        Context{std::move(prompt), std::move(reference), face},
        false, 0, {}, false, {}, true
    );
}

/**
 * Replaces an unfinished generation request, then executes after its cleanup on the lifecycle queue.
 *
 * @param[in] context Conditions to commit on success; an empty value reuses the last committed conditions.
 * @param[in] microphone Whether to enable microphone capture if a connection must be created.
 */
uint64_t XmaxRealtimeManager::request_generation(
    std::optional<Context> context,
    bool microphone,
    uint64_t local_revision,
    std::optional<bool> face_restoration,
    bool connect_if_needed,
    Cancel caller_cancelled,
    bool update_only
) {
    require(!local_revision || local_revision == local_stream_revision(),
            XMAX_INVALID_STATE, "The local media stream has been replaced");
    require(!caller_cancelled || !caller_cancelled(), XMAX_CANCELLED, "Generation cancelled");
    require(!update_only || lifecycle_.state() == XMAX_GENERATING, XMAX_INVALID_STATE, "No generation is active");
    if (context) {
        context->prompt = trim_utf8_whitespace(context->prompt);
        context->reference = trim_utf8_whitespace(context->reference);
    }
    uint64_t remote_revision = 0;
    lifecycle_.run(
        OperationKind::Generation,
        [this, incoming = std::move(context), microphone, local_revision, face_restoration,
         connect_if_needed, caller_cancelled = std::move(caller_cancelled), update_only, &remote_revision](const Lifecycle::Token& token) {
            const auto cancel = [operation_cancelled = token.cancellation(), caller_cancelled] {
                return operation_cancelled() || (caller_cancelled && caller_cancelled());
            };
            require(!cancel(), XMAX_CANCELLED, "Generation cancelled");
            const auto state = lifecycle_.state();
            require(!update_only || state == XMAX_GENERATING, XMAX_INVALID_STATE, "No generation is active");
            const auto needs_connection = connect_if_needed && can_connect();
            require(
                needs_connection || state == XMAX_CONNECTED || state == XMAX_GENERATING,
                XMAX_INVALID_STATE,
                "Realtime connection is not open"
            );
            require(!local_revision || local_revision == local_stream_revision(),
                    XMAX_INVALID_STATE, "The local media stream has been replaced");
            require(
                incoming.has_value() || generation_.context.has_value(),
                XMAX_INVALID_ARGUMENT,
                "Initial generation conditions are required"
            );
            auto value = incoming ? *incoming : *generation_.context;
            if (face_restoration)
                value.face = *face_restoration;
            const auto previous_context = generation_.context;
            const auto measures_startup = state != XMAX_GENERATING;
            if (measures_startup) {
                token.failure_scope(Scope::Connection);
                timing_.begin();
            }
            try {
                if (needs_connection) {
                    connect_impl(
                        cancel,
                        lifecycle_.epoch(),
                        microphone
                    );
                    change_state(XMAX_CONNECTED);
                }
                const auto started = generate_impl(
                    value,
                    cancel
                );
                token.ensure_current();
                require(!cancel(), XMAX_CANCELLED, "Generation cancelled");
                remote_revision = lifecycle_.epoch() + 1;
                complete_state(
                    XMAX_GENERATING,
                    token,
                    [this, started] {
                        if (started) {
                            timing_.finish();
                            emit_timing(timing_.snapshot());
                        }
                    }
                );
            } catch (...) {
                generation_.context = previous_context;
                if (measures_startup) {
                    try {
                        throw;
                    } catch (const Error& error) {
                        if (const auto timing = timing_.failure(error))
                            emit_timing(*timing);
                    } catch (...) {
                        if (const auto timing = timing_.failure(Error(
                                XMAX_INTERNAL_ERROR,
                                "Generation startup failed"
                            )))
                            emit_timing(*timing);
                    }
                }
                throw;
            }
        }
    );
    return remote_revision;
}

} // namespace xmax::detail
