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
#include "Foundation/Runtime/RuntimeInfo.hpp"

namespace xmax::detail {

RoomMessageSender::RoomMessageSender() : worker_([this] { run(); }) {
}

RoomMessageSender::~RoomMessageSender() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        *room_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

std::shared_future<void> RoomMessageSender::enqueue(
    std::shared_ptr<Rtc> rtc,
    const json& message,
    Cancel cancelled
) {
    auto value = std::make_shared<Transmission>();
    value->rtc = std::move(rtc);
    value->packets = encode_message(
        message.dump(),
        random_id()
    );
    value->cancelled = std::move(cancelled);
    auto result = value->completion.get_future().share();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        require(
            !stopped_ && !resetting_,
            XMAX_CANCELLED,
            "RTC message room is closed"
        );
        require(
            queue_.size() + size_t(busy_) < 64,
            XMAX_RTC_ERROR,
            "RTC room message queue is full"
        );
        value->room = room_;
        queue_.push_back(std::move(value));
    }
    wake_.notify_one();
    return result;
}

void RoomMessageSender::reset() {
    std::unique_lock<std::mutex> lock(mutex_);
    resetting_ = true;
    *room_ = true;
    wake_.notify_all();
    wake_.wait(lock, [this] { return queue_.empty() && !busy_; });
    sent_.clear();
    room_ = std::make_shared<std::atomic<bool>>(false);
    resetting_ = false;
}

void RoomMessageSender::run() {
    for (;;) {
        std::shared_ptr<Transmission> value;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
            if (queue_.empty())
                return;
            value = queue_.front();
            queue_.pop_front();
            busy_ = true;
        }

        try {
            transmit(*value);
            value->completion.set_value();
        } catch (...) {
            value->completion.set_exception(std::current_exception());
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
        wake_.notify_all();
    }
}

void RoomMessageSender::transmit(
    const Transmission& value
) {
    const auto ensure_current = [&] {
        require(
            !value.room->load() && !value.cancelled(),
            XMAX_CANCELLED,
            "RTC message cancelled"
        );
    };
    for (const auto& packet : value.packets) {
        for (;;) {
            ensure_current();
            const auto now = Clock::now();
            while (!sent_.empty() && now - sent_.front().first >= std::chrono::seconds(1))
                sent_.pop_front();
            size_t bytes = 0;
            for (const auto& entry : sent_)
                bytes += entry.second;
            if (sent_.size() < 58 && bytes + packet.size() <= 29000)
                break;
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(5));
        }
        ensure_current();
        value.rtc->send(packet);
        sent_.emplace_back(Clock::now(), packet.size());
    }
}
} // namespace xmax::detail
