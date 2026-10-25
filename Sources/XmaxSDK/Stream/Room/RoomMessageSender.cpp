//
//  RoomMessageSender.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomMessageSender.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include <thread>

namespace xmax::detail {

/**
 * Sends UTF-8 fragments under a sliding budget, checking cancellation and maintaining keepalives while
 * waiting.
 *
 * @param[in] rtc A borrowed connected RTC adapter that can send signaling fragments.
 * @param[in] message The room JSON message to serialize, fragment, and send.
 * @param[in] cancelled A cooperative cancellation predicate; true requests termination of the operation.
 * @param[in] maintain The maintenance callback invoked while waiting or throttled; empty omits maintenance.
 */
void RoomMessageSender::send(
    Rtc& rtc,
    const json& message,
    const Cancel& cancelled,
    const std::function<void()>& maintain
) {
    const auto packets = encode_message(
        message.dump(),
        random_id()
    );
    XmaxLogger::write(
        LogCategory::Room,
        XMAX_LOG_DEBUG,
        [&](auto language) {
            // Exclude generation conditions, prompts, reference URLs, credentials, and raw fragments from logs.
            return XmaxLogger::localized(
                language,
                "发送房间信令 (Send Room Signal)",
                "Send Room Signal"
            ) +
                   "\n└─ packets=" + std::to_string(packets.size());
        }
    );
    for (const auto& packet : packets) {
        for (;;) {
            require(
                !cancelled(),
                XMAX_CANCELLED,
                "Operation cancelled"
            );

            // Maintain keepalives during long sends; keepalive messages omit this callback to prevent recursion.
            if (maintain)
                maintain();

            const auto now = Clock::now();

            while (!sent_.empty() && now - sent_.front().first >= std::chrono::seconds(1))
                sent_.pop_front();

            size_t bytes = 0;

            for (const auto& p : sent_)
                bytes += p.second;

            if (sent_.size() < 28 && bytes + packet.size() <= 5800)
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        rtc.send(packet);
        sent_.emplace_back(
            Clock::now(),
            packet.size()
        );
    }
}
} // namespace xmax::detail
