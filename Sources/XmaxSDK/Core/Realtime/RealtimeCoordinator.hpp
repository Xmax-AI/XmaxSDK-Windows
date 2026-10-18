#pragma once
#include "Foundation/Common.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
namespace xmax::detail {
// Serial lifecycle execution, cancellation generations, state and bounded application events.
class RealtimeCoordinator {
  public:
    explicit RealtimeCoordinator(std::function<void()> tick);
    ~RealtimeCoordinator();
    void shutdown();
    void sync(std::function<void()>);
    void enqueue(std::function<void()>);
    uint64_t epoch() const { return epoch_.load(); }
    uint64_t invalidate() { return ++epoch_; }
    Cancel cancellation(uint64_t);
    xmax_state state() const { return state_.load(); }
    void state(xmax_state value) { state_ = value; }
    void emit(xmax_event);
    bool poll(xmax_event&);
    // Timer methods are confined to the lifecycle executor.
    void deadline(Clock::time_point value) { deadline_ = value; }
    Clock::time_point deadline() const { return deadline_; }

  private:
    std::function<void()> tick_;
    std::thread worker_;
    std::mutex work_mutex_, event_mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> work_;
    std::deque<xmax_event> events_;
    bool exiting_ = false;
    std::atomic<uint64_t> epoch_{0};
    std::atomic<xmax_state> state_{XMAX_IDLE};
    Clock::time_point deadline_ = Clock::time_point::max();
    void run();
};
} // namespace xmax::detail
