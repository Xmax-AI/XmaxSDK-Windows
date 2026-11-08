//
//  MicrophoneCaptureController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/Audio/MicrophoneCaptureController.hpp"
#include "Media/Audio/WasapiMicrophone.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Media/Audio/MicrophoneDevices.hpp"

namespace xmax::detail {

MicrophoneCaptureController::MicrophoneCaptureController(MicrophoneValidator validator, MicrophoneFactory factory)
    : validator_(validator ? std::move(validator) : validate_microphone_device),
      factory_(std::move(factory)) {
    if (!factory_) {
        factory_ = [](const std::string& device, auto packet, auto level, auto failure) {
            return std::make_unique<WasapiMicrophone>(device, std::move(packet), std::move(level), std::move(failure));
        };
    }
}

MicrophoneCaptureController::~MicrophoneCaptureController() { stop(); }

void MicrophoneCaptureController::validate(const std::string& device) const { validator_(device); }

void MicrophoneCaptureController::configure(bool enabled, std::string device) {
    enabled_ = enabled;
    device_ = std::move(device);
}

void MicrophoneCaptureController::select(std::string device) {
    if (enabled_ || capture_)
        validate(device);
    if (device == device_)
        return;
    if (capture_) {
        auto replacement = open(device);
        capture_.reset();
        if (active_)
            clear_level();
        capture_ = std::move(replacement);
        if (active_)
            capture_->activate();
    }
    device_ = std::move(device);
}

void MicrophoneCaptureController::prepare(MicrophoneCapture::Packet packet, MicrophoneCapture::Level level) {
    stop();
    capture_ = open(device_);
    packet_ = std::move(packet);
    level_ = std::move(level);
}

void MicrophoneCaptureController::activate() noexcept {
    active_ = true;
    capture_->activate();
}

void MicrophoneCaptureController::stop() noexcept {
    capture_.reset();
    if (active_)
        clear_level();
    active_ = false;
    packet_ = {};
    level_ = {};
}

void MicrophoneCaptureController::close() noexcept {
    stop();
    enabled_ = false;
    device_.clear();
    packet_end_us_ = 0;
}

std::unique_ptr<MicrophoneCapture> MicrophoneCaptureController::open(const std::string& device) {
    return factory_(
        device,
        [this](const ExternalAudioPacket& packet) {
            // A replacement endpoint can still hold samples from before the switch.
            if (packet.timestamp_us < packet_end_us_)
                return;
            packet_end_us_ = packet.timestamp_us + 10000;
            packet_(packet);
        },
        [this](int level) {
            if (level_)
                level_(level);
        },
        [this](const Error& error) {
            clear_level();
            XmaxLogger::write(LogCategory::Media, XMAX_LOG_WARNING, [&](auto) {
                return std::string("Microphone capture interrupted: ") + error.what();
            });
        });
}

void MicrophoneCaptureController::clear_level() noexcept {
    if (level_) {
        try { level_(-1); } catch (...) {}
    }
}

} // namespace xmax::detail
