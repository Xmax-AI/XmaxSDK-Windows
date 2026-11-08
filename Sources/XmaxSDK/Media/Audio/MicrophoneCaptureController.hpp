//
//  MicrophoneCaptureController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/ExternalAudioPacket.hpp"
#include <memory>

namespace xmax::detail {

// Opens capture in a paused state. Destruction drains the capture callbacks.
class MicrophoneCapture {
  public:
    using Packet = std::function<void(const ExternalAudioPacket&)>;
    using Level = std::function<void(int)>;
    using Failure = std::function<void(const Error&)>;

    virtual ~MicrophoneCapture() = default;
    virtual void activate() noexcept = 0;
    virtual bool running() const noexcept = 0;
};

using MicrophoneValidator = std::function<void(const std::string&)>;
using MicrophoneFactory = std::function<std::unique_ptr<MicrophoneCapture>(
    const std::string&, MicrophoneCapture::Packet, MicrophoneCapture::Level, MicrophoneCapture::Failure)>;

// Owns device selection, capture and level delivery. Control methods run on the lifecycle worker.
class MicrophoneCaptureController {
  public:
    explicit MicrophoneCaptureController(MicrophoneValidator validator = {}, MicrophoneFactory factory = {});
    ~MicrophoneCaptureController();

    void validate(const std::string& device) const;
    void validate() const { validate(device_); }
    void configure(bool enabled, std::string device);
    void select(std::string device);

    bool enabled() const { return enabled_; }
    void enabled(bool enabled) { enabled_ = enabled; }
    bool running() const { return capture_ && capture_->running(); }

    // Prepares the device before joining and defers delivery until publication is ready.
    void prepare(MicrophoneCapture::Packet packet, MicrophoneCapture::Level level);
    void activate() noexcept;
    void stop() noexcept;
    void close() noexcept;

  private:
    std::unique_ptr<MicrophoneCapture> open(const std::string& device);
    void clear_level() noexcept;

    MicrophoneValidator validator_;
    MicrophoneFactory factory_;
    std::string device_;
    bool enabled_ = false;
    bool active_ = false;
    MicrophoneCapture::Packet packet_;
    MicrophoneCapture::Level level_;
    int64_t packet_end_us_ = 0;
    std::unique_ptr<MicrophoneCapture> capture_;
};

} // namespace xmax::detail
