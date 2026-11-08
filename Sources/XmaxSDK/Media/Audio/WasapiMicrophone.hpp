//
//  WasapiMicrophone.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Media/Audio/MicrophoneCaptureController.hpp"
#include <atomic>
#include <memory>
#include <thread>

namespace xmax::detail {

// Captures the selected Windows recording endpoint without opening playback or an RTC device.
class WasapiMicrophone final : public MicrophoneCapture {
  public:
    WasapiMicrophone(const std::string& device, Packet packet, Level level, Failure failure);
    ~WasapiMicrophone() override;

    // A replacement source can be started before switching delivery away from the previous input.
    void activate() noexcept override { active_ = true; }
    bool running() const noexcept override { return running_; }

  private:
    std::atomic<bool> active_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    struct StopEvent;
    std::unique_ptr<StopEvent> stop_event_;
    std::thread worker_;

    void stop() noexcept;
};

} // namespace xmax::detail
