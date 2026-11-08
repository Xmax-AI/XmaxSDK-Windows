//
//  RtcMediaSubmissionMetrics.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Logging/XmaxLogger.hpp"
#include <algorithm>
#include <mutex>

namespace xmax::detail {

/** Separates engine-lock wait from the time spent submitting a media packet. */
class RtcMediaSubmissionMetrics {
  public:
    void record(const char* kind, Clock::time_point queued, Clock::time_point acquired,
                Clock::time_point submitted) noexcept {
        try {
            const auto generation = XmaxLogger::configuration().generation;
            std::lock_guard<std::mutex> lock(mutex_);
            if (begin_ == Clock::time_point{} || generation_ != generation) {
                begin_ = queued;
                generation_ = generation;
                count_ = 0;
                wait_ms_ = submit_ms_ = wait_max_ms_ = submit_max_ms_ = 0;
            }

            const auto wait = std::chrono::duration<double, std::milli>(acquired - queued).count();
            const auto submit = std::chrono::duration<double, std::milli>(submitted - acquired).count();
            ++count_;
            wait_ms_ += wait;
            submit_ms_ += submit;
            wait_max_ms_ = std::max(wait_max_ms_, wait);
            submit_max_ms_ = std::max(submit_max_ms_, submit);
            if (submitted - begin_ < std::chrono::seconds(2))
                return;

            XmaxLogger::write(LogCategory::Stream, XMAX_LOG_DEBUG, [&](auto) {
                return std::string("RTC ") + kind + " submission" +
                       "\n├─ packets=" + std::to_string(count_) +
                       ", wait_avg_ms=" + std::to_string(wait_ms_ / count_) +
                       ", wait_max_ms=" + std::to_string(wait_max_ms_) +
                       "\n└─ submit_avg_ms=" + std::to_string(submit_ms_ / count_) +
                       ", submit_max_ms=" + std::to_string(submit_max_ms_);
            }, XMAX_LOG_PERFORMANCE);
            begin_ = {};
        } catch (...) {
            // Diagnostics must not interrupt media delivery.
        }
    }

  private:
    std::mutex mutex_;
    Clock::time_point begin_{};
    uint64_t generation_ = 0;
    uint64_t count_ = 0;
    double wait_ms_ = 0, submit_ms_ = 0;
    double wait_max_ms_ = 0, submit_max_ms_ = 0;
};

} // namespace xmax::detail
