#include "Core/Realtime/RealtimeCoordinator.hpp"
#include <algorithm>
namespace xmax::detail {
RealtimeCoordinator::RealtimeCoordinator(std::function<void()> tick) : tick_(std::move(tick)) {
    worker_ = std::thread([this] { run(); });
}
RealtimeCoordinator::~RealtimeCoordinator() {
    shutdown();
}
void RealtimeCoordinator::shutdown() {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        exiting_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void RealtimeCoordinator::enqueue(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        work_.push_back(std::move(fn));
    }
    wake_.notify_one();
}
void RealtimeCoordinator::sync(std::function<void()> fn) {
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
    auto result = task->get_future();
    enqueue([task] { (*task)(); });
    result.get();
}
void RealtimeCoordinator::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(work_mutex_);
            wake_.wait_until(lock, deadline_, [this] { return exiting_ || !work_.empty(); });
            if (exiting_ && work_.empty())
                return;
            if (!work_.empty()) {
                job = std::move(work_.front());
                work_.pop_front();
            }
        }
        if (job)
            job();
        if (Clock::now() >= deadline_)
            tick_();
    }
}
Cancel RealtimeCoordinator::cancellation(uint64_t epoch) {
    return [this, epoch] { return epoch_.load() != epoch; };
}
void RealtimeCoordinator::emit(xmax_event event) {
    event.struct_size = sizeof(event);
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.size() >= 256) {
        auto old = std::find_if(events_.begin(), events_.end(),
                                [](const auto& e) { return e.type == XMAX_EVENT_STATISTICS; });
        if (old != events_.end())
            events_.erase(old);
        else
            events_.pop_front();
    }
    events_.push_back(event);
}
bool RealtimeCoordinator::poll(xmax_event& event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.empty())
        return false;
    event = events_.front();
    events_.pop_front();
    return true;
}
} // namespace xmax::detail
