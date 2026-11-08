//
//  WasapiMicrophone.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/Audio/WasapiMicrophone.hpp"
#ifdef _WIN32
#include "Foundation/Media/Audio/WindowsRecordingEndpoint.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>

namespace xmax::detail {
namespace {

constexpr uint32_t sample_rate = 48000;
constexpr size_t packet_samples = 480;
constexpr int64_t packet_duration_us = 10000;

class CaptureApartment {
  public:
    CaptureApartment() {
        check_microphone(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize microphone capture thread");
    }
    ~CaptureApartment() { CoUninitialize(); }
};

class CaptureEvent {
  public:
    explicit CaptureEvent(bool manual_reset) : event_(CreateEventW(nullptr, manual_reset, FALSE, nullptr)) {
        require(event_ != nullptr, XMAX_MEDIA_ERROR, "Cannot create microphone capture event");
    }

    ~CaptureEvent() { CloseHandle(event_); }
    CaptureEvent(const CaptureEvent&) = delete;
    CaptureEvent& operator=(const CaptureEvent&) = delete;
    HANDLE get() const noexcept { return event_; }

  private:
    HANDLE event_;
};

struct Packetizer {
    ExternalAudioPacket packet;
    int64_t next_timestamp = 0;
    int64_t next_level = 0;

    Packetizer() { packet.samples.reserve(packet_samples); }

    void append(const int16_t* samples, size_t count, int64_t timestamp, bool discontinuous,
                const WasapiMicrophone::Packet& send, const WasapiMicrophone::Level& level) {
        const auto expected = next_timestamp + int64_t(packet.samples.size()) * 1000000 / sample_rate;
        if (next_timestamp == 0 || discontinuous || timestamp > expected + 20000) {
            packet.samples.clear();
            next_timestamp = std::max(timestamp, next_timestamp);
        }
        while (count) {
            const auto chunk = std::min(count, packet_samples - packet.samples.size());
            packet.samples.insert(packet.samples.end(), samples, samples + chunk);
            samples += chunk;
            count -= chunk;
            if (packet.samples.size() != packet_samples)
                continue;
            packet.timestamp_us = next_timestamp;
            next_timestamp += packet_duration_us;
            if (level && packet.timestamp_us >= next_level) {
                int peak = 0;
                for (const auto value : packet.samples)
                    peak = std::max(peak, std::abs(int(value)));
                level((peak * 255 + 16384) / 32768);
                next_level = packet.timestamp_us + 200000;
            }
            send(packet);
            packet.samples.clear();
        }
    }
};

struct CaptureInterrupted : Error {
    using Error::Error;
};

void check_read(HRESULT status, const char* operation) {
    if (status == AUDCLNT_E_DEVICE_INVALIDATED || status == AUDCLNT_E_RESOURCES_INVALIDATED ||
        status == AUDCLNT_E_SERVICE_NOT_RUNNING)
        throw CaptureInterrupted(XMAX_MEDIA_ERROR, "Windows microphone input was interrupted");
    check_microphone(status, operation);
}

class CaptureSession {
  public:
    explicit CaptureSession(const std::string& device) : audio_(open_recording_endpoint(device)) {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = sample_rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = sizeof(int16_t);
        format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
        check_microphone(audio_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0, 0, &format, nullptr), "Initialize microphone PCM capture");
        check_microphone(audio_->SetEventHandle(ready_.get()), "Set microphone capture event");
        check_microphone(audio_->GetService(IID_PPV_ARGS(&capture_)), "Open microphone PCM stream");
        UINT32 buffer_frames = 0;
        check_microphone(audio_->GetBufferSize(&buffer_frames), "Read microphone buffer size");
        samples_.resize(buffer_frames);
        check_microphone(audio_->Start(), "Start microphone capture");
    }

    ~CaptureSession() { audio_->Stop(); }

    HANDLE ready_event() const noexcept { return ready_.get(); }

    void drain(const std::atomic<bool>& active, const std::atomic<bool>& stopping,
               const WasapiMicrophone::Packet& packet, const WasapiMicrophone::Level& level) {
        UINT32 count = 0;
        check_read(capture_->GetNextPacketSize(&count), "Read microphone packet size");
        while (count != 0 && !stopping) {
            BYTE* data = nullptr;
            DWORD flags = 0;
            UINT64 time_100ns = 0;
            check_read(capture_->GetBuffer(&data, &count, &flags, nullptr, &time_100ns), "Read microphone PCM");
            if (count == 0)
                return;
            if (count > samples_.size()) {
                capture_->ReleaseBuffer(count);
                throw Error(XMAX_MEDIA_ERROR, "Microphone packet exceeds its capture buffer");
            }
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                std::fill_n(samples_.begin(), count, int16_t{0});
            else
                std::memcpy(samples_.data(), data, count * sizeof(int16_t));
            check_read(capture_->ReleaseBuffer(count), "Release microphone packet");
            if (active) {
                const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                    Clock::now().time_since_epoch()).count();
                const auto timestamp = (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) || time_100ns == 0
                    ? now - int64_t(count) * 1000000 / sample_rate : int64_t(time_100ns / 10);
                output_.append(samples_.data(), count, timestamp, (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0,
                               packet, level);
            }
            check_read(capture_->GetNextPacketSize(&count), "Read microphone packet size");
        }
    }

  private:
    CaptureEvent ready_{false};
    Microsoft::WRL::ComPtr<IAudioClient> audio_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_;
    std::vector<int16_t> samples_;
    Packetizer output_;
};

} // namespace

struct WasapiMicrophone::StopEvent {
    CaptureEvent event{true};
};

WasapiMicrophone::WasapiMicrophone(const std::string& device, Packet packet, Level level, Failure failure)
    : stop_event_(std::make_unique<StopEvent>()) {
    std::promise<void> started;
    auto ready = started.get_future();
    worker_ = std::thread([this, device, packet = std::move(packet), level = std::move(level),
                           failure = std::move(failure), started = std::move(started)]() mutable {
        bool capture_started = false;
        try {
            CaptureApartment apartment;
            const bool follows_default = device.empty();
            auto selected = follows_default ? default_recording_endpoint_id() : device;
            auto session = std::make_unique<CaptureSession>(selected);
            auto route_check = Clock::now() + std::chrono::milliseconds(500);
            const auto suspend = [&](const Error& error) {
                if (session && active_ && failure)
                    failure(error);
                session.reset();
            };

            running_ = true;
            started.set_value();
            capture_started = true;
            for (;;) {
                const HANDLE events[] = {stop_event_->event.get(), session ? session->ready_event() : nullptr};
                // Check for invalidated or stalled devices even when the audio engine stops signaling.
                const auto ready_event = WaitForMultipleObjects(session ? 2 : 1, events, FALSE, session ? 20 : 500);
                if (ready_event == WAIT_OBJECT_0)
                    break;
                require(ready_event != WAIT_FAILED, XMAX_MEDIA_ERROR, "Cannot wait for microphone capture");
                if (follows_default && (!session || Clock::now() >= route_check)) {
                    route_check = Clock::now() + std::chrono::milliseconds(500);
                    try {
                        auto current = default_recording_endpoint_id();
                        if (current != selected) {
                            selected = std::move(current);
                            if (session && active_ && level)
                                level(-1);
                            session.reset();
                        }
                    } catch (const Error& error) {
                        suspend(error);
                        continue;
                    }
                }
                if (!session) {
                    try { session = std::make_unique<CaptureSession>(selected); }
                    catch (const Error&) { continue; }
                }
                try { session->drain(active_, stopping_, packet, level); }
                catch (const CaptureInterrupted& error) { suspend(error); }
            }
        } catch (...) {
            running_ = false;
            if (!capture_started)
                started.set_exception(std::current_exception());
            else if (active_ && failure) {
                try { throw; }
                catch (const Error& error) { try { failure(error); } catch (...) {} }
                catch (...) { try { failure(Error(XMAX_MEDIA_ERROR, "Microphone capture failed")); } catch (...) {} }
            }
        }
        running_ = false;
    });
    try { ready.get(); }
    catch (...) { stop(); throw; }
}

WasapiMicrophone::~WasapiMicrophone() { stop(); }

void WasapiMicrophone::stop() noexcept {
    active_ = false;
    stopping_ = true;
    SetEvent(stop_event_->event.get());
    if (worker_.joinable())
        worker_.join();
}

} // namespace xmax::detail
#else

namespace xmax::detail {

struct WasapiMicrophone::StopEvent {};

WasapiMicrophone::WasapiMicrophone(const std::string&, Packet, Level, Failure) {
    throw Error(XMAX_UNSUPPORTED, "Windows microphone capture is unavailable on this platform");
}
WasapiMicrophone::~WasapiMicrophone() = default;

} // namespace xmax::detail

#endif
