//
//  RoomCommandController.Transmission.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomCommandController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {

void RoomCommandController::collect_transmissions_locked(
    const std::shared_ptr<Command>& command
) {
    for (auto at = command->attempts.begin(); at != command->attempts.end();) {
        const auto& attempt = *at;
        if (!attempt->result.valid() ||
            attempt->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++at;
            continue;
        }
        finish_attempt_locked(
            command,
            *attempt
        );
        at = command->attempts.erase(at);
    }

    if (command->relay_active && command->relay.valid() &&
        command->relay.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        command->relay.get();
        command->relay_active = false;
    }
}

void RoomCommandController::finish_attempt_locked(
    const std::shared_ptr<Command>& command,
    const Attempt& attempt
) {
    try {
        attempt.result.get();
        if (!command->cancelled->load()) {
            command->sent = true;
            if (!command->settled) {
                command->settled = true;
                command->completion.set_value();
            }
        }
    } catch (...) {
        if (attempt.initial && !command->sent && !command->cancelled->load()) {
            cancel_locked(
                command,
                std::current_exception()
            );
            if (command->kind == "start") {
                for (const auto& dependent : commands_) {
                    if (dependent != command && dependent->task == command->task)
                        cancel_locked(dependent);
                }
            }
        } else if (command->retrying && !command->cancelled->load() && !attempt.cancelled->load()) {
            XmaxLogger::write(
                LogCategory::Room,
                XMAX_LOG_WARNING,
                [&](auto) {
                    return "RTC command retry send failed: " + command->kind + ", xid=" + command->xid;
                }
            );
        }
    }
}

void RoomCommandController::launch_relay(
    const std::shared_ptr<Command>& command
) {
    try {
        auto task = std::async(
            std::launch::async,
            [command, relay = relay_] {
                const Cancel cancelled = [command] {
                    return command->cancelled->load() || command->validity();
                };
                try {
                    require(
                        !cancelled(),
                        XMAX_CANCELLED,
                        "Event relay cancelled"
                    );
                    relay(
                        command->message,
                        cancelled
                    );
                } catch (const std::exception& error) {
                    if (!cancelled()) {
                        XmaxLogger::write(
                            LogCategory::Room,
                            XMAX_LOG_WARNING,
                            [&](auto) {
                                return "HTTP signaling relay failed: event=" + command->kind +
                                       ", xid=" + command->xid + ", " + XmaxLogger::redact(error.what());
                            }
                        );
                    }
                } catch (...) {
                    // Foreign transport exceptions must not escape the background HTTP task.
                }
            }
        );
        std::lock_guard<std::mutex> lock(mutex_);
        command->relay = std::move(task);
    } catch (const std::exception&) {
        std::lock_guard<std::mutex> lock(mutex_);
        command->relay_active = false;
        XmaxLogger::write(
            LogCategory::Room,
            XMAX_LOG_WARNING,
            [&](auto) { return "HTTP signaling relay could not start: xid=" + command->xid; }
        );
    }
}

void RoomCommandController::launch_attempt(
    const Launch& launch
) {
    const auto& command = launch.first;
    const auto& attempt = launch.second;
    std::shared_future<void> result;
    try {
        const Cancel cancelled = [command, attempt] {
            return command->cancelled->load() || attempt->cancelled->load() || command->validity();
        };
        require(
            !cancelled(),
            XMAX_CANCELLED,
            "RTC command cancelled"
        );
        result = send_(
            command->message,
            cancelled
        );
        require(
            result.valid(),
            XMAX_INTERNAL_ERROR,
            "RTC sender returned no completion"
        );
    } catch (...) {
        std::promise<void> failure;
        failure.set_exception(std::current_exception());
        result = failure.get_future().share();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    attempt->result = std::move(result);
}
} // namespace xmax::detail
