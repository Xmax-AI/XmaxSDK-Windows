#pragma once
#include "Foundation/RTC/RtcPlatform.hpp"
#include "Stream/Room/MessageCodec.hpp"
#include <deque>
namespace xmax::detail {
// Serial room signaling: UTF-8 fragmentation and a sliding-window transport budget.
class RoomMessageSender {
  public:
    void send(Rtc&, const json&, const Cancel&, const std::function<void()>& maintain = {});
    void reset() { sent_.clear(); }

  private:
    std::deque<std::pair<Clock::time_point, size_t>> sent_;
};
} // namespace xmax::detail
