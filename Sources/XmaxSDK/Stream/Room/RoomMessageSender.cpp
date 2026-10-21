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
 * 按滑动预算发送 UTF-8 信令分片，等待期间检查取消并执行保活。
 *
 * @param[in] rtc 已连接且可发送信令分片的 RTC 适配器借用引用。
 * @param[in] message 待序列化并分片发送的房间 JSON 信令。
 * @param[in] cancelled 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] maintain 等待或限速期间执行的维护回调，空函数表示无需额外维护。
 */
void RoomMessageSender::send(
    Rtc& rtc,
    const json& message,
    const Cancel& cancelled,
    const std::function<void()>& maintain
) {
    const auto packets = encode_message(message.dump(), random_id());
    XmaxLogger::write(LogCategory::Room, XMAX_LOG_DEBUG, [&](auto language) {
        // 日志不输出生成条件、提示词、参考地址、房间凭证和原始信令分片。
        return XmaxLogger::localized(language, "发送房间信令 (Send Room Signal)", "Send Room Signal") +
               "\n└─ packets=" + std::to_string(packets.size());
    });
    for (const auto& packet : packets) {
        for (;;) {
            require(!cancelled(), XMAX_CANCELLED, "Operation cancelled");

            // 较长的信令发送期间持续执行保活；保活信令不附带该回调，避免递归发送。
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
        sent_.emplace_back(Clock::now(), packet.size());
    }
}
} // namespace xmax::detail
