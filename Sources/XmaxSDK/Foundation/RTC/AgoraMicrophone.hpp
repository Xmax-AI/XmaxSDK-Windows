//
//  AgoraMicrophone.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace agora::rtc {
class IRtcEngine;
}

namespace xmax::detail {

/** Owns physical microphone selection, native capture and RTC publication policy. */
class AgoraMicrophone {
  public:
    void select(agora::rtc::IRtcEngine* active_engine, const std::string& device);
    void prepare(agora::rtc::IRtcEngine& engine, bool enabled);
    void enable(agora::rtc::IRtcEngine& engine, bool enabled);

    /** Reports whether physical capture is requested for the current RTC engine. */
    bool enabled() const { return enabled_.load(); }

    /** Reports whether samples can be attributed to the active physical input. */
    bool accepts_samples() const { return enabled_ && available_; }

    void availability(bool available) { available_ = available; }

  private:
    std::string device_;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> available_{false};
};

} // namespace xmax::detail
