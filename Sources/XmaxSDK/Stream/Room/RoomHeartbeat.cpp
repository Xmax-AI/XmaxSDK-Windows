//
//  RoomHeartbeat.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomHeartbeat.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {

RoomHeartbeat::~RoomHeartbeat() {
    stop();
}

void RoomHeartbeat::start(
    RoomMessageSender& sender,
    std::shared_ptr<Rtc> rtc,
    json message,
    std::chrono::milliseconds interval
) {
    stop();
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    worker_ = std::thread([this, &sender, rtc = std::move(rtc), message = std::move(message), interval,
                           cancelled = cancelled_] {
        const auto cancel = [cancelled] { return cancelled->load(); };
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (wake_.wait_for(lock, interval, cancel))
                    return;
            }
            try {
                auto result = sender.enqueue(rtc, message, cancel);
                while (result.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
                    if (cancel())
                        return;
                }
                result.get();
            } catch (const std::exception& error) {
                if (!cancel())
                    XmaxLogger::write(LogCategory::Room, XMAX_LOG_ERROR, [&](auto language) {
                        return XmaxLogger::localized(language, "发送 RTC 房间心跳失败", "Failed to send RTC room heartbeat") +
                               "\n└─ " + error.what();
                    });
            }
        }
    });
}

void RoomHeartbeat::stop() noexcept {
    if (!cancelled_)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_->store(true);
    }
    wake_.notify_one();
    if (worker_.joinable())
        worker_.join();
    cancelled_.reset();
}

} // namespace xmax::detail
