//
//  RoomCommandController.Scheduling.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomCommandController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include <algorithm>
#include <iterator>

namespace xmax::detail {

void RoomCommandController::run() {
    for (;;) {
        std::vector<Launch> launches;
        std::vector<std::shared_ptr<Command>> relays;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(5));

            bool preceding_settled = true;
            for (const auto& command : commands_) {
                collect_transmissions_locked(command);
                if (!command->cancelled->load() && command->validity())
                    cancel_locked(command);

                if (!closed_ && !command->cancelled->load()) {
                    schedule_locked(
                        command,
                        preceding_settled,
                        launches,
                        relays
                    );
                }
                preceding_settled = preceding_settled && command->settled;
            }

            commands_.erase(
                std::remove_if(
                    commands_.begin(),
                    commands_.end(),
                    [](const auto& command) {
                        return command->settled && !command->retrying && command->attempts.empty() &&
                               !command->relay_active;
                    }
                ),
                commands_.end()
            );
            if (closed_ && commands_.empty())
                return;
        }

        // Transport adapters may invoke callbacks synchronously; keep them outside the state lock.
        for (const auto& command : relays)
            launch_relay(command);
        for (const auto& launch : launches)
            launch_attempt(launch);
    }
}

void RoomCommandController::schedule_locked(
    const std::shared_ptr<Command>& command,
    bool preceding_settled,
    std::vector<Launch>& launches,
    std::vector<std::shared_ptr<Command>>& relays
) {
    if (!command->begun && preceding_settled) {
        command->begun = true;
        command->began_at = now_();
        if (relay_) {
            command->relay_active = true;
            relays.push_back(command);
        }
    }
    if (!command->begun || !command->retrying)
        return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now_() - command->began_at
    ).count();
    if (elapsed >= 6000) {
        stop_retries_locked(*command);
        XmaxLogger::write(
            LogCategory::Room,
            XMAX_LOG_WARNING,
            [&](auto) {
                return "RTC command ACK not received after 6000 ms; retries stopped: " +
                       command->kind + ", xid=" + command->xid;
            }
        );
        return;
    }

    constexpr int delays[]{0, 300, 1000, 3000, 3300, 4000};
    while (command->next < std::size(delays) && elapsed >= delays[command->next]) {
        auto attempt = std::make_shared<Attempt>();
        attempt->initial = command->next++ == 0;
        command->attempts.push_back(attempt);
        launches.emplace_back(
            command,
            attempt
        );
    }
}
} // namespace xmax::detail
