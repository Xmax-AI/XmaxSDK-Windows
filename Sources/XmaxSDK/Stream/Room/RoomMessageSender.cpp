#include "Stream/Room/RoomMessageSender.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include <thread>
namespace xmax::detail {
void RoomMessageSender::send(Rtc& rtc, const json& message, const Cancel& cancelled,
                             const std::function<void()>& maintain) {
    const auto packets = encode_message(message.dump(), random_id());
    for (const auto& packet : packets) {
        for (;;) {
            require(!cancelled(), XMAX_CANCELLED, "Operation cancelled");
            // Long chunked conditions may span several heartbeat intervals.
            // The maintenance signal itself uses send() without this hook to avoid recursion.
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
