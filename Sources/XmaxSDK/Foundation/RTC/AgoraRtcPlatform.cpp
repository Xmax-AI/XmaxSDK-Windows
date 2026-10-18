#include "Foundation/RTC/RtcPlatform.hpp"
#include <IAgoraMediaEngine.h>
#include <IAgoraRtcEngine.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
namespace xmax::detail {
using namespace agora;
using namespace agora::rtc;
using namespace agora::media;
namespace media_base = agora::media::base;
namespace {
std::mutex engine_lease;
bool engine_in_use = false;
void check_rtc(int code, const char* operation) {
    if (code < 0)
        throw Error(XMAX_RTC_ERROR, std::string(operation) + " failed (" + std::to_string(code) + ")");
}
class AgoraRtc final : public Rtc, private IRtcEngineEventHandler, private IVideoFrameObserver {
    bool owns_lease_ = false;
    std::mutex engine_mutex_, metadata_mutex_;
    std::condition_variable changed_;
    IRtcEngine* engine_ = nullptr;
    IMediaEngine* media_ = nullptr;
    std::atomic<bool> stopping_{false}, joined_{false};
    bool first_frame_ = false;
    float volume_ = 0;
    std::string failure_, room_, bot_;
    std::atomic<uid_t> bot_uid_{0};
    int stream_id_ = 0;
    RtcCallbacks callbacks_;
    std::vector<uint8_t> packed_;
    std::optional<int64_t> timestamp_offset_ms_;
    void wait(bool first, const Cancel& cancel, const std::function<void()>& keepalive = [] {}) {
        const auto deadline = Clock::now() + std::chrono::seconds(first ? 30 : 15);
        std::unique_lock<std::mutex> lock(metadata_mutex_);
        while (!(first ? first_frame_ : joined_.load())) {
            lock.unlock();
            keepalive();
            lock.lock();
            require(!cancel(), XMAX_CANCELLED, "RTC operation cancelled");
            require(failure_.empty(), XMAX_RTC_ERROR, "RTC connection failed");
            require(Clock::now() < deadline, XMAX_TIMEOUT,
                    first ? "Timed out waiting for generated video" : "RTC join timed out");
            changed_.wait_for(lock, std::chrono::milliseconds(20));
        }
    }
    void failure(const char* message) {
        if (stopping_)
            return;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            failure_ = message;
        }
        changed_.notify_all();
        if (callbacks_.failure)
            callbacks_.failure(Error(XMAX_RTC_ERROR, message));
    }

  public:
    AgoraRtc() {
        std::lock_guard<std::mutex> lock(engine_lease);
        require(!engine_in_use, XMAX_INVALID_STATE,
                "Another Xmax RTC session owns the process-wide Agora engine");
        engine_in_use = true;
        owns_lease_ = true;
    }
    ~AgoraRtc() override { leave(); }
    void join(const Connection& c, VideoEncodingConfiguration encoding, bool mic, RtcCallbacks cb,
              const Cancel& cancel) override {
        room_ = c.room;
        bot_ = c.bot;
        const auto f = encoding.format;
        callbacks_ = std::move(cb);
        engine_ = createAgoraRtcEngine();
        require(engine_, XMAX_RTC_ERROR, "Cannot create Agora engine");
        RtcEngineContext context;
        context.appId = c.app_id.c_str();
        context.eventHandler = this;
        context.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
        check_rtc(engine_->initialize(context), "initialize");
        check_rtc(engine_->enableVideo(), "enableVideo");
        check_rtc(engine_->enableLocalVideo(false), "enableLocalVideo");
        check_rtc(engine_->enableLocalAudio(mic), "enableLocalAudio");
        check_rtc(engine_->queryInterface(AGORA_IID_MEDIA_ENGINE, reinterpret_cast<void**>(&media_)),
                  "query media engine");
        check_rtc(media_->setExternalVideoSource(true, false), "setExternalVideoSource");
        check_rtc(media_->registerVideoFrameObserver(this), "registerVideoFrameObserver");
        VideoEncoderConfiguration encoder;
        encoder.dimensions = VideoDimensions(int(f.width), int(f.height));
        encoder.frameRate = int(f.fps);
        encoder.minBitrate = encoding.minimum_bitrate;
        encoder.bitrate = encoding.maximum_bitrate;
        encoder.degradationPreference =
            encoding.preference == XMAX_ENCODER_AUTO
                ? MAINTAIN_AUTO
                : (encoding.preference == XMAX_ENCODER_MAINTAIN_QUALITY ? MAINTAIN_QUALITY
                                                                        : MAINTAIN_FRAMERATE);
        encoder.mirrorMode = VIDEO_MIRROR_MODE_DISABLED;
        check_rtc(engine_->setVideoEncoderConfiguration(encoder), "setVideoEncoderConfiguration");
        check_rtc(engine_->adjustPlaybackSignalVolume(int(volume_ * 100)), "adjustPlaybackSignalVolume");
        ChannelMediaOptions options;
        options.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
        options.clientRoleType = CLIENT_ROLE_BROADCASTER;
        options.publishCameraTrack = false;
        options.publishCustomVideoTrack = true;
        options.publishMicrophoneTrack = mic;
        options.autoSubscribeVideo = true;
        options.autoSubscribeAudio = false;
        check_rtc(
            engine_->joinChannelWithUserAccount(c.token.c_str(), c.room.c_str(), c.user.c_str(), options),
            "joinChannelWithUserAccount");
        wait(false, cancel);
        DataStreamConfig stream;
        stream.ordered = true;
        stream.syncWithAudio = false;
        check_rtc(engine_->createDataStream(&stream_id_, stream), "createDataStream");
    }
    void send(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(engine_ && joined_ && !stopping_, XMAX_RTC_ERROR, "RTC room is not joined");
        check_rtc(engine_->sendStreamMessage(stream_id_, bytes.data(), bytes.size()), "sendStreamMessage");
    }
    void push(const Frame& f) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        if (!engine_ || !media_ || !joined_ || stopping_)
            return;
        media_base::ExternalVideoFrame frame;
        frame.format = f.format == XMAX_BGRA ? media_base::VIDEO_PIXEL_BGRA
                                             : (f.format == XMAX_NV12 ? media_base::VIDEO_PIXEL_NV12
                                                                      : media_base::VIDEO_PIXEL_I420);
        frame.stride = int(f.width);
        frame.height = int(f.height);
        const auto source_ms = f.timestamp_us / 1000;
        if (!timestamp_offset_ms_)
            timestamp_offset_ms_ = engine_->getCurrentMonotonicTimeInMs() - source_ms;
        frame.timestamp = source_ms + *timestamp_offset_ms_;
        if (f.format == XMAX_BGRA)
            frame.buffer = const_cast<uint8_t*>(f.planes[0].data());
        else {
            packed_.clear();
            for (const auto& plane : f.planes)
                packed_.insert(packed_.end(), plane.begin(), plane.end());
            frame.buffer = packed_.data();
        }
        check_rtc(media_->pushVideoFrame(&frame), "pushVideoFrame");
    }
    void wait_remote(const Cancel& cancel, const std::function<void()>& keepalive) override {
        wait(true, cancel, keepalive);
    }
    void begin_generation() override {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        first_frame_ = false;
    }
    void volume(float volume) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        if (engine_ && !stopping_)
            check_rtc(engine_->adjustPlaybackSignalVolume(int(volume * 100)), "adjustPlaybackSignalVolume");
        volume_ = volume;
    }
    void configure_encoding(VideoEncodingConfiguration value) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(engine_ && !stopping_, XMAX_INVALID_STATE, "RTC is not connected");
        VideoEncoderConfiguration encoder;
        encoder.dimensions = VideoDimensions(int(value.format.width), int(value.format.height));
        encoder.frameRate = int(value.format.fps);
        encoder.minBitrate = value.minimum_bitrate;
        encoder.bitrate = value.maximum_bitrate;
        encoder.degradationPreference =
            value.preference == XMAX_ENCODER_AUTO
                ? MAINTAIN_AUTO
                : (value.preference == XMAX_ENCODER_MAINTAIN_QUALITY ? MAINTAIN_QUALITY : MAINTAIN_FRAMERATE);
        encoder.mirrorMode = VIDEO_MIRROR_MODE_DISABLED;
        check_rtc(engine_->setVideoEncoderConfiguration(encoder), "setVideoEncoderConfiguration");
    }
    void leave() noexcept override {
        stopping_ = true;
        joined_ = false;
        changed_.notify_all();
        IRtcEngine* engine = nullptr;
        IMediaEngine* media = nullptr;
        {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            engine = std::exchange(engine_, nullptr);
            media = std::exchange(media_, nullptr);
        }
        // No application locks across blocking release: Agora drains callback threads here.
        if (media) {
            media->registerVideoFrameObserver(nullptr);
            media->release();
        }
        if (engine) {
            engine->leaveChannel();
            engine->release();
        }
        // The last shared reference may be dropped by the media thread. A lease
        // cannot be represented by a mutex held for the object's whole lifetime.
        std::lock_guard<std::mutex> lock(engine_lease);
        if (owns_lease_) {
            engine_in_use = false;
            owns_lease_ = false;
        }
    }

  private:
    void onJoinChannelSuccess(const char*, uid_t, int) override {
        joined_ = true;
        changed_.notify_all();
    }
    void onError(int, const char*) override { failure("Agora reported an RTC error"); }
    void onConnectionLost() override { failure("RTC connection was lost"); }
    void onFirstLocalVideoFramePublished(VIDEO_SOURCE_TYPE, int) override {
        if (!stopping_ && callbacks_.published)
            callbacks_.published();
    }
    void onUserJoined(uid_t uid, int) override {
        if (stopping_)
            return;
        rtc::UserInfo info{};
        int result = -1;
        {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            if (engine_ && !stopping_)
                result = engine_->getUserInfoByUid(uid, &info);
        }
        if (result == 0)
            onUserInfoUpdated(uid, info);
    }
    void onUserInfoUpdated(uid_t uid, const rtc::UserInfo& info) override {
        if (stopping_ || bot_ != info.userAccount)
            return;
        bot_uid_ = uid;
        std::lock_guard<std::mutex> lock(engine_mutex_);
        if (engine_ && !stopping_)
            engine_->muteRemoteAudioStream(uid, false);
    }
    void onLocalAudioStateChanged(LOCAL_AUDIO_STREAM_STATE state, LOCAL_AUDIO_STREAM_REASON) override {
        if (!stopping_ && state == LOCAL_AUDIO_STREAM_STATE_FAILED)
            failure("Microphone capture failed; check device access and permissions");
    }
    void onLocalVideoStats(VIDEO_SOURCE_TYPE, const LocalVideoStats& stats) override {
        if (stopping_)
            return;
        xmax_event e{};
        e.type = XMAX_EVENT_STATISTICS;
        e.direction = 0;
        e.width = stats.encodedFrameWidth;
        e.height = stats.encodedFrameHeight;
        e.fps = stats.sentFrameRate;
        e.bitrate_kbps = stats.sentBitrate;
        e.packet_loss_percent = stats.txPacketLossRate;
        e.quality = -1;
        e.e2e_ms = -1;
        if (callbacks_.event)
            callbacks_.event(e);
        const auto adaptation = int(stats.qualityAdaptIndication);
        if ((adaptation == 1 || adaptation == 2) && callbacks_.event) {
            e.type = XMAX_EVENT_PERFORMANCE;
            e.code = adaptation == 2 ? 1 : 0;
            e.fps = stats.encoderOutputFrameRate;
            callbacks_.event(e);
        }
    }
    void onRemoteVideoStats(const RemoteVideoStats& stats) override {
        if (stopping_ || stats.uid != bot_uid_)
            return;
        xmax_event e{};
        e.type = XMAX_EVENT_STATISTICS;
        e.direction = 1;
        e.width = stats.width;
        e.height = stats.height;
        e.fps = stats.rendererOutputFrameRate;
        e.bitrate_kbps = stats.receivedBitrate;
        e.packet_loss_percent = stats.packetLossRate;
        e.e2e_ms = stats.e2eDelay;
        e.quality = -1;
        if (callbacks_.event)
            callbacks_.event(e);
    }
    void onNetworkQuality(uid_t uid, int up, int down) override {
        if (stopping_ || uid != 0)
            return;
        for (int direction = 0; direction < 2; ++direction) {
            xmax_event e{};
            e.type = XMAX_EVENT_STATISTICS;
            e.direction = direction;
            e.quality = direction ? down : up;
            e.width = e.height = e.fps = e.bitrate_kbps = e.packet_loss_percent = e.e2e_ms = -1;
            if (callbacks_.event)
                callbacks_.event(e);
        }
    }
    bool onCaptureVideoFrame(VIDEO_SOURCE_TYPE, VideoFrame&) override { return true; }
    bool onPreEncodeVideoFrame(VIDEO_SOURCE_TYPE, VideoFrame&) override { return true; }
    bool onMediaPlayerVideoFrame(VideoFrame&, int) override { return true; }
    bool onTranscodedVideoFrame(VideoFrame&) override { return true; }
    media_base::VIDEO_PIXEL_FORMAT getVideoFormatPreference() override {
        return media_base::VIDEO_PIXEL_BGRA;
    }
    uint32_t getObservedFramePosition() override { return media_base::POSITION_PRE_RENDERER; }
    bool getRotationApplied() override { return true; }
    bool getMirrorApplied() override { return false; }
    bool onRenderVideoFrame(const char* channel, uid_t uid, VideoFrame& video) override {
        if (stopping_ || !channel || room_ != channel || bot_uid_ == 0 || uid != bot_uid_)
            return true;
        try {
            require(video.type == media_base::VIDEO_PIXEL_BGRA && video.width > 0 && video.height > 0 &&
                        video.yStride > 0,
                    XMAX_RTC_ERROR, "Unsupported decoded video format");
            xmax_video_frame v{};
            v.struct_size = sizeof(v);
            v.pixel_format = XMAX_BGRA;
            v.width = uint32_t(video.width);
            v.height = uint32_t(video.height);
            v.timestamp_us = std::max<int64_t>(0, video.renderTimeMs) * 1000;
            v.planes[0] = video.yBuffer;
            size_t stride = size_t(video.yStride);
            const size_t row_bytes = size_t(video.width) * 4;
            // Accept packed total byte count as documented by Agora, or row stride.
            if (stride == row_bytes * size_t(video.height))
                stride = row_bytes;
            else if (stride >= size_t(video.width) && stride < row_bytes)
                stride *= 4;
            v.strides[0] = stride;
            v.plane_sizes[0] = stride * size_t(video.height);
            auto frame = Frame::copy(v);
            // RTC video is opaque; decoders may leave the otherwise unused BGRA alpha byte at zero.
            for (size_t i = 3; i < frame.planes[0].size(); i += 4)
                frame.planes[0][i] = 255;
            if (callbacks_.frame)
                callbacks_.frame(std::move(frame));
            {
                std::lock_guard<std::mutex> lock(metadata_mutex_);
                first_frame_ = true;
            }
            changed_.notify_all();
        } catch (const std::exception&) {
            failure("Cannot copy a decoded RTC video frame");
        }
        return true;
    }
};
} // namespace
bool rtc_available() noexcept {
    return true;
}
std::shared_ptr<Rtc> make_rtc() {
    return std::make_shared<AgoraRtc>();
}
} // namespace xmax::detail
