//
//  AgoraRtcPlatform.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/RTC/RtcStatsLogger.hpp"
#include "Foundation/RTC/RtcMediaSubmissionMetrics.hpp"
#include "Foundation/RTC/RtcPlatform.hpp"
#include "Foundation/Text/Utf8.hpp"
#include <IAgoraMediaEngine.h>
#include <IAgoraRtcEngine.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace xmax::detail {
using namespace agora;
using namespace agora::rtc;
using namespace agora::media;
namespace media_base = agora::media::base;

namespace {

/**
 * The mutex protecting the process-wide exclusive RTC engine flag.
 */
std::mutex engine_lease;

/**
 * Whether a native RTC engine adapter is active in this process.
 */
bool engine_in_use = false;

/**
 * Translates negative native RTC results to SDK exceptions preserving the native code.
 *
 * @param[in] code The native RTC result; 0 succeeds and nonzero results become SDK errors.
 * @param[in] operation The diagnostic name of the native RTC operation.
 */
void check_rtc(
    int code,
    const char* operation
) {
    if (code < 0)
        throw Error(
            XMAX_RTC_ERROR,
            std::string(operation) + " failed (" + std::to_string(code) + ")"
        );
}

/**
 * Manages the RTC engine, external publication, and result observation; only one engine instance may be
 * active per process.
 */
class AgoraRtc final : public Rtc,
                       private IRtcEngineEventHandler,
                       private IVideoFrameObserver,
                       private agora::media::IAudioFrameObserver {

    /**
     * The native PCM callback layout, distinct from the SDK's owned audio packet.
     */
    using NativeAudioFrame = agora::media::IAudioFrameObserverBase::AudioFrame;

    /**
     * The native audio callback configuration type.
     */
    using NativeAudioParams = agora::media::IAudioFrameObserverBase::AudioParams;

    /**
     * Whether this adapter holds the process-wide exclusive engine lease.
     */
    bool owns_lease_ = false;

    /**
     * Whether the custom PCM track is published.
     */
    bool audio_published_ = false;

    /**
     * The custom direct-audio track, or the native invalid-track sentinel before allocation.
     */
    static constexpr rtc::track_id_t invalid_audio_track = 0xffffffff;
    rtc::track_id_t audio_track_ = invalid_audio_track;

    /**
     * The mutex protecting native engine calls and lifecycle.
     */
    std::mutex engine_mutex_;

    /**
     * The mutex protecting join, first-result, and failure metadata.
     */
    std::mutex metadata_mutex_;

    /**
     * Notifies waiters of room join, first result, or RTC failure.
     */
    std::condition_variable changed_;

    /**
     * The native RTC engine owned by this adapter.
     */
    IRtcEngine* engine_ = nullptr;

    /**
     * The engine's borrowed media interface for external frame submission.
     */
    IMediaEngine* media_ = nullptr;

    /**
     * Whether RTC is stopping; callbacks reject further delivery.
     */
    std::atomic<bool> stopping_{false};

    /**
     * Whether current native PCM callbacks should copy result-user audio for application output.
     */
    std::atomic<bool> audio_observing_{false};
    std::atomic<bool> result_audio_enabled_{false};

    /**
     * Whether RTC successfully joined the current room.
     */
    std::atomic<bool> joined_{false};

    /**
     * Whether the current generation has received its first result frame.
     */
    bool generation_result_lost_ = false;

    /**
     * The result-user playback volume in [0, 1], protected by the native engine mutex.
     */
    float volume_ = 0;

    /**
     * The latest redacted RTC failure reason, shared with waiters and error callbacks.
     */
    std::string failure_;

    /**
     * The current native error's SDK category, protected by the metadata mutex.
     */
    xmax_result failure_code_ = XMAX_RTC_ERROR;

    /**
     * The current RTC room identifier.
     */
    std::string room_;

    /**
     * The account identifier of the service result user.
     */
    std::string bot_;

    /**
     * The resolved result user's UID used to filter frames and statistics.
     */
    std::atomic<uid_t> bot_uid_{0};

    /** Publication notices received before the remote account has resolved, guarded by the engine mutex. */
    std::unordered_map<uid_t, bool> video_publications_;

    /** Whether the current result user's video is subscribed, guarded by the metadata mutex. */
    bool result_video_subscribed_ = false;

    /** Whether the current generation start command has been confirmed, guarded by the metadata mutex. */
    bool generation_confirmed_ = false;

    /** Invalidates decoded frames copied concurrently with publication and task changes. */
    uint64_t result_video_revision_ = 0;

    /**
     * The reliable native data stream identifier used for room signaling.
     */
    int stream_id_ = 0;

    /**
     * The connection's frame, event, failure, and publication callbacks.
     */
    RtcCallbacks callbacks_;

    /**
     * Serializes video submissions and protects the contiguous buffer for multiplane input.
     */
    std::mutex video_mutex_;
    std::vector<uint8_t> packed_;
    RtcMediaSubmissionMetrics video_submission_, audio_submission_;

    /**
     * The millisecond offset from media time to RTC monotonic time, established on first submission.
     */
    std::optional<int64_t> timestamp_offset_ms_;

    /**
     * The guard keeping RTC credentials registered for log redaction.
     */
    std::shared_ptr<const std::string> log_token_;

    /**
     * Waits for room join or result publication until the operation deadline.
     *
     * @param[in] first True waits for result publication; false waits for room join.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void wait(
        bool result,
        const Cancel& cancel,
        Clock::time_point deadline
    ) {
        std::unique_lock<std::mutex> lock(metadata_mutex_);
        for (;;) {
            require(
                !cancel(),
                XMAX_CANCELLED,
                "RTC operation cancelled"
            );
            require(!result || !generation_result_lost_, XMAX_CANCELLED, "Result video was unpublished");
            require(
                failure_.empty(),
                failure_code_,
                failure_
            );
            require(
                Clock::now() < deadline,
                XMAX_TIMEOUT,
                result ? "Realtime generation start timed out" : "RTC join timed out"
            );
            if (result ? (generation_confirmed_ && result_video_subscribed_) : joined_.load())
                return;
            changed_.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(20)));
        }
    }

    /**
     * Retains a redacted RTC error, wakes waiters, and notifies the internal failure receiver.
     *
     * @param[in] message The NUL-terminated RTC failure message, redacted before storage and delivery.
     * @param[in] code The SDK category corresponding to the native error.
     */
    void failure(
        const char* message,
        xmax_result code = XMAX_RTC_ERROR
    ) {
        if (stopping_)
            return;

        const auto safe = XmaxLogger::redact(message);
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            if (!failure_.empty())
                return;
            failure_ = safe;
            failure_code_ = code;
        }
        XmaxLogger::write(
            LogCategory::RTC,
            XMAX_LOG_ERROR,
            [&](auto language) {
                return XmaxLogger::localized(
                           language,
                           "RTC 失败 (RTC Failure)",
                           "RTC Failure"
                       ) +
                       "\n└─ " + safe;
            }
        );
        changed_.notify_all();

        if (callbacks_.failure)
            callbacks_.failure(Error(
                code,
                safe
            ));
    }

  public:

    /**
     * Acquires the process-wide engine lease, rejecting conflicts before server-session allocation.
     */
    AgoraRtc() {
        std::lock_guard<std::mutex> lock(engine_lease);
        require(
            !engine_in_use,
            XMAX_INVALID_STATE,
            "Another Xmax RTC session owns the process-wide Agora engine"
        );
        engine_in_use = true;
        owns_lease_ = true;
    }

    /**
     * Leaves RTC and releases the exclusive engine lease.
     */
    ~AgoraRtc() override {
        leave();
    }

    /**
     * Initializes RTC, configures external video and audio policy, and waits for room join.
     *
     * @param[in] c The validated app, room, user, token, and service-result account configuration.
     * @param[in] encoding The resolved encoding dimensions, frame rate, bitrate, and degradation policy.
     * @param[in] publish_audio Whether to publish a custom PCM track on join.
     * @param[in] cb Callbacks for result frames, RTC events, failures, and first publication; drained before
     * stop returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void join(
        const Connection& c,
        VideoEncodingConfiguration encoding,
        bool publish_audio,
        RtcCallbacks cb,
        const Cancel& cancel
    ) override {
        log_token_ = XmaxLogger::protect_secret(c.token);
        room_ = c.room;
        bot_ = c.bot;

        const auto f = encoding.format;
        callbacks_ = std::move(cb);
        engine_ = createAgoraRtcEngine();
        require(
            engine_,
            XMAX_RTC_ERROR,
            "Cannot create Agora engine"
        );

        RtcEngineContext context;
        context.appId = c.app_id.c_str();
        context.eventHandler = this;
        context.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
        check_rtc(
            engine_->initialize(context),
            "initialize"
        );
        check_rtc(
            engine_->enableVideo(),
            "enableVideo"
        );
        check_rtc(
            engine_->enableLocalVideo(false),
            "enableLocalVideo"
        );
        // The SDK supplies PCM on a custom track. Keep the vendor recording device closed.
        check_rtc(engine_->enableLocalAudio(false), "disable native microphone capture");
        check_rtc(
            engine_->queryInterface(
                AGORA_IID_MEDIA_ENGINE,
                reinterpret_cast<void**>(&media_)
            ),
            "query media engine"
        );
        check_rtc(
            media_->setExternalVideoSource(
                true,
                false
            ),
            "setExternalVideoSource"
        );
        check_rtc(
            media_->registerVideoFrameObserver(this),
            "registerVideoFrameObserver"
        );

        check_rtc(
            engine_->setPlaybackAudioFrameBeforeMixingParameters(
                48000,
                1
            ),
            "setPlaybackAudioFrameBeforeMixingParameters"
        );
        check_rtc(
            media_->registerAudioFrameObserver(this),
            "registerAudioFrameObserver"
        );

        VideoEncoderConfiguration encoder;
        encoder.dimensions = VideoDimensions(
            int(f.width),
            int(f.height)
        );
        encoder.frameRate = int(f.fps);
        encoder.orientationMode =
            f.width >= f.height ? ORIENTATION_MODE_FIXED_LANDSCAPE : ORIENTATION_MODE_FIXED_PORTRAIT;
        encoder.minBitrate = encoding.minimum_bitrate;
        encoder.bitrate = encoding.maximum_bitrate;
        encoder.degradationPreference =
            encoding.preference == XMAX_ENCODER_AUTO
                ? MAINTAIN_AUTO
                : (encoding.preference == XMAX_ENCODER_MAINTAIN_QUALITY ? MAINTAIN_QUALITY
                                                                        : MAINTAIN_FRAMERATE);
        encoder.mirrorMode = encoding.mirrored ? VIDEO_MIRROR_MODE_ENABLED : VIDEO_MIRROR_MODE_DISABLED;
        check_rtc(
            engine_->setVideoEncoderConfiguration(encoder),
            "setVideoEncoderConfiguration"
        );
        check_rtc(
            engine_->adjustPlaybackSignalVolume(100),
            "adjustPlaybackSignalVolume"
        );

        if (publish_audio)
            ensure_audio_track();

        ChannelMediaOptions options;
        options.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
        options.clientRoleType = CLIENT_ROLE_BROADCASTER;
        options.publishCameraTrack = false;
        options.publishCustomVideoTrack = true;
        options.publishMicrophoneTrack = false;
        options.publishCustomAudioTrack = publish_audio;
        if (publish_audio)
            options.publishCustomAudioTrackId = int(audio_track_);
        options.autoSubscribeVideo = false;
        options.autoSubscribeAudio = false;
        check_rtc(
            engine_->joinChannelWithUserAccount(
                c.token.c_str(),
                c.room.c_str(),
                c.user.c_str(),
                options
            ),
            "joinChannelWithUserAccount"
        );
        wait(
            false,
            cancel,
            Clock::now() + std::chrono::seconds(15)
        );

        audio_published_ = publish_audio;

        DataStreamConfig stream;
        stream.ordered = true;
        stream.syncWithAudio = false;
        check_rtc(
            engine_->createDataStream(
                &stream_id_,
                stream
            ),
            "createDataStream"
        );
    }

    void audio_publication(bool enabled) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(engine_ && joined_ && !stopping_, XMAX_INVALID_STATE, "No joined RTC room");
        if (enabled == audio_published_)
            return;
        if (enabled)
            ensure_audio_track();
        ChannelMediaOptions options;
        options.publishMicrophoneTrack = false;
        options.publishCustomAudioTrack = enabled;
        options.publishCustomAudioTrackId = int(audio_track_);
        check_rtc(engine_->updateChannelMediaOptions(options), "Update custom PCM publication");
        audio_published_ = enabled;
    }

    /**
     * Sends one room signaling fragment and propagates native transport failures.
     *
     * @param[in] bytes The encoded bytes to process; they must remain valid until this synchronous call
     * returns.
     */
    void send(
        const std::string& bytes
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(
            engine_ && joined_ && !stopping_,
            XMAX_RTC_ERROR,
            "RTC room is not joined"
        );
        check_rtc(
            engine_->sendStreamMessage(
                stream_id_,
                bytes.data(),
                bytes.size()
            ),
            "sendStreamMessage"
        );
    }

    /**
     * Publishes owned external pixels using the native monotonic clock offset.
     *
     * @param[in] f The frame to validate or deliver, with pixel storage valid throughout the call.
     */
    void push(
        const Frame& f
    ) override {
        if (!joined_ || stopping_)
            return;

        // Keep planar packing outside the engine lock shared with PCM and signaling.
        std::lock_guard<std::mutex> video_lock(video_mutex_);
        media_base::ExternalVideoFrame frame;
        frame.format = f.format == XMAX_BGRA ? media_base::VIDEO_PIXEL_BGRA
                                             : (f.format == XMAX_NV12 ? media_base::VIDEO_PIXEL_NV12
                                                                      : media_base::VIDEO_PIXEL_I420);
        frame.stride = int(f.width);
        frame.height = int(f.height);
        if (f.format == XMAX_BGRA) {
            frame.buffer = const_cast<uint8_t*>(f.planes[0].data());
        } else {
            packed_.clear();
            packed_.reserve(f.planes[0].size() + f.planes[1].size() + f.planes[2].size());
            for (const auto& plane : f.planes)
                packed_.insert(packed_.end(), plane.begin(), plane.end());
            frame.buffer = packed_.data();
        }

        const bool profiling = XmaxLogger::is_enabled(XMAX_LOG_PERFORMANCE);
        const auto queued = profiling ? Clock::now() : Clock::time_point{};
        std::unique_lock<std::mutex> engine_lock(engine_mutex_);
        if (!engine_ || !media_ || !joined_ || stopping_)
            return;
        const auto acquired = profiling ? Clock::now() : Clock::time_point{};
        const auto source_ms = f.timestamp_us / 1000;
        if (!timestamp_offset_ms_)
            timestamp_offset_ms_ = engine_->getCurrentMonotonicTimeInMs() - source_ms;
        frame.timestamp = source_ms + *timestamp_offset_ms_;

        const auto result = media_->pushVideoFrame(&frame);
        const auto submitted = profiling ? Clock::now() : Clock::time_point{};
        engine_lock.unlock();
        if (profiling)
            video_submission_.record("video", queued, acquired, submitted);
        check_rtc(result, "pushVideoFrame");
    }

    /**
     * Uploads one 10 ms mono PCM16 packet using the same RTC clock offset as external video.
     *
     * @param[in] source The 480 copied samples and shared source timestamp in microseconds.
     */
    void push_audio(
        const ExternalAudioPacket& source
    ) override {
        const bool profiling = XmaxLogger::is_enabled(XMAX_LOG_PERFORMANCE);
        const auto queued = profiling ? Clock::now() : Clock::time_point{};
        std::unique_lock<std::mutex> lock(engine_mutex_);
        if (!media_ || !engine_ || !joined_ || stopping_ || !audio_published_)
            return;
        const auto acquired = profiling ? Clock::now() : Clock::time_point{};
        require(
            source.samples.size() == 480 && source.timestamp_us >= 0,
            XMAX_MEDIA_ERROR,
            "External RTC audio requires a timestamped 10 ms PCM packet"
        );
        const auto source_ms = source.timestamp_us / 1000;
        if (!timestamp_offset_ms_)
            timestamp_offset_ms_ = engine_->getCurrentMonotonicTimeInMs() - source_ms;
        NativeAudioFrame frame;
        frame.samplesPerChannel = 480;
        frame.bytesPerSample = TWO_BYTES_PER_SAMPLE;
        frame.channels = 1;
        frame.samplesPerSec = 48000;
        frame.buffer = const_cast<int16_t*>(source.samples.data());
        frame.renderTimeMs = source_ms + *timestamp_offset_ms_;

        const auto result = media_->pushAudioFrame(&frame, audio_track_);
        const auto submitted = profiling ? Clock::now() : Clock::time_point{};
        lock.unlock();
        if (profiling)
            audio_submission_.record("audio", queued, acquired, submitted);
        check_rtc(result, "pushAudioFrame");
    }

    /**
     * Waits for the selected result user to publish video.
     *
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void wait_remote(const Cancel& cancel, Clock::time_point deadline) override {
        wait(true, cancel, deadline);
    }

    /**
     * Resets publication confirmation for the next generation.
     */
    void begin_generation() override {
        end_generation();
    }

    void confirm_generation() override {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        generation_confirmed_ = true;
        changed_.notify_all();
    }

    void end_generation() override {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        ++result_video_revision_;
        if (std::exchange(generation_confirmed_, false))
            clear_downlink_statistics();
        generation_result_lost_ = false;
    }

    void result_audio(bool enabled) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        const auto uid = bot_uid_.load();
        bool subscribed;
        {
            std::lock_guard<std::mutex> result_lock(metadata_mutex_);
            require(!enabled || !generation_result_lost_, XMAX_CANCELLED, "Result video was unpublished");
            subscribed = result_video_subscribed_ && generation_confirmed_;
        }
        update_result_audio(uid, enabled && subscribed);
    }

    /**
     * Retains result-user volume and updates playback when its remote account is available.
     *
     * @param[in] volume A finite volume in [0, 1]; 0 mutes playback.
     */
    void volume(
        float volume
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        const auto uid = bot_uid_.load();
        if (uid && !stopping_ && engine_)
            check_rtc(
                engine_->adjustUserPlaybackSignalVolume(uid, int(std::lround(volume * 100))),
                "adjustUserPlaybackSignalVolume"
            );
        volume_ = volume;
    }

    /**
     * Enables result-user PCM observation without changing playback volume or audio subscription policy.
     *
     * @param[in] enabled Whether to copy remote PCM for application delivery.
     */
    void observe_remote_media(
        bool enabled
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(
            engine_ && media_ && !stopping_,
            XMAX_INVALID_STATE,
            "RTC is not connected"
        );
        audio_observing_ = enabled;
    }

    /**
     * Applies effective dimensions, frame rate, bitrate limits, and degradation preference.
     *
     * @param[in] value The resolved encoding dimensions, frame rate, bitrate limits, and degradation
     * preference.
     */
    void configure_encoding(
        VideoEncodingConfiguration value
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(
            engine_ && !stopping_,
            XMAX_INVALID_STATE,
            "RTC is not connected"
        );

        VideoEncoderConfiguration encoder;
        encoder.dimensions = VideoDimensions(
            int(value.format.width),
            int(value.format.height)
        );
        encoder.frameRate = int(value.format.fps);
        encoder.orientationMode = value.format.width >= value.format.height ? ORIENTATION_MODE_FIXED_LANDSCAPE
                                                                            : ORIENTATION_MODE_FIXED_PORTRAIT;
        encoder.minBitrate = value.minimum_bitrate;
        encoder.bitrate = value.maximum_bitrate;
        encoder.degradationPreference =
            value.preference == XMAX_ENCODER_AUTO
                ? MAINTAIN_AUTO
                : (value.preference == XMAX_ENCODER_MAINTAIN_QUALITY ? MAINTAIN_QUALITY : MAINTAIN_FRAMERATE);
        encoder.mirrorMode = value.mirrored ? VIDEO_MIRROR_MODE_ENABLED : VIDEO_MIRROR_MODE_DISABLED;
        check_rtc(
            engine_->setVideoEncoderConfiguration(encoder),
            "setVideoEncoderConfiguration"
        );
    }

    /**
     * Stops publication, releases the engine outside callback locks, and drains callback delivery.
     */
    void leave() noexcept override {
        stopping_ = true;
        joined_ = false;
        changed_.notify_all();

        IRtcEngine* engine = nullptr;
        IMediaEngine* media = nullptr;
        {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            engine = std::exchange(
                engine_,
                nullptr
            );
            media = std::exchange(
                media_,
                nullptr
            );
        }

        // Engine release waits for callback threads; do not hold application mutexes here.
        audio_observing_ = false;
        result_audio_enabled_ = false;
        if (media) {
            if (audio_track_ != invalid_audio_track) {
                if (engine)
                    engine->leaveChannel();
                media->destroyCustomAudioTrack(audio_track_);
                audio_track_ = invalid_audio_track;
            }
            media->registerAudioFrameObserver(nullptr);
            media->registerVideoFrameObserver(nullptr);
            media->release();
        }

        if (engine) {
            engine->leaveChannel();
            engine->release();
        }

        // The last reference may be released on a media thread; the engine lease must not retain a thread-owned lock.
        std::lock_guard<std::mutex> lock(engine_lease);

        if (owns_lease_) {
            engine_in_use = false;
            owns_lease_ = false;
        }
    }

  private:

    void ensure_audio_track() {
        if (audio_track_ != invalid_audio_track)
            return;
        AudioTrackConfig configuration;
        configuration.enableLocalPlayback = false;
        configuration.enableAudioProcessing = false;
        audio_track_ = media_->createCustomAudioTrack(AUDIO_TRACK_DIRECT, configuration);
        require(audio_track_ != invalid_audio_track, XMAX_RTC_ERROR, "Cannot create custom PCM track");
    }

    /**
     * Records successful room join and wakes join waiters.
     *
     * @param[in] channel The RTC room name that triggered the callback.
     * @param[in] uid The native numeric RTC user identifier used to identify and filter the result stream.
     * @param[in] elapsed The operation duration reported by the native SDK, in milliseconds.
     */
    void onJoinChannelSuccess(
        [[maybe_unused]] const char* channel,
        [[maybe_unused]] uid_t uid,
        [[maybe_unused]] int elapsed
    ) override {
        joined_ = true;
        changed_.notify_all();
    }

    void onError(int code, const char* description) override {
        if (!stopping_) {
            XmaxLogger::write(LogCategory::RTC, XMAX_LOG_ERROR,
                [&](auto) { return XmaxLogger::redact(RtcStatsLogger::error_message(code, description)); });
        }
    }

    void onConnectionStateChanged(CONNECTION_STATE_TYPE state, CONNECTION_CHANGED_REASON_TYPE reason) override {
        if (reason == CONNECTION_CHANGED_TOKEN_EXPIRED)
            onRequestToken();
        else if (state == CONNECTION_STATE_FAILED)
            failure(("RTC connection failed (" + std::to_string(int(reason)) + ")").c_str());
    }

    void onRequestToken() override {
        failure("RTC token expired");
    }

    /**
     * Records startup timing when the native engine confirms first publication.
     *
     * @param[in] source_type The native video source type.
     * @param[in] elapsed The operation duration reported by the native SDK, in milliseconds.
     */
    void onFirstLocalVideoFramePublished(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        [[maybe_unused]] int elapsed
    ) override {
        if (!stopping_ && callbacks_.published)
            callbacks_.published();
    }

    /**
     * Copies bounded signaling from the resolved result account before returning to the native engine.
     */
    void onStreamMessage(
        uid_t uid,
        [[maybe_unused]] int stream_id,
        const char* data,
        size_t length,
        [[maybe_unused]] uint64_t sent_at
    ) override {
        if (stopping_ || !data || length > 262144 || bot_uid_ == 0 || uid != bot_uid_ || !callbacks_.message)
            return;
        try {
            callbacks_.message(bot_, std::string(data, length));
        } catch (...) {
            // Malformed receipts must not escape into the native RTC callback.
        }
    }

    /**
     * Identifies the result user and applies the retained playback volume.
     *
     * @param[in] uid The native numeric RTC user identifier used to identify and filter the result stream.
     * @param[in] elapsed The operation duration reported by the native SDK, in milliseconds.
     */
    void onUserJoined(
        uid_t uid,
        [[maybe_unused]] int elapsed
    ) override {
        if (stopping_)
            return;

        rtc::UserInfo info{};
        int result = -1;
        {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            if (engine_ && !stopping_)
                result = engine_->getUserInfoByUid(
                    uid,
                    &info
                );
        }

        if (result == 0)
            onUserInfoUpdated(
                uid,
                info
            );
    }

    /**
     * Clears downlink statistics when the result user leaves, without reusing its UID on reconnection.
     *
     * @param[in] uid The departing native user identifier.
     * @param[in] reason The native user-offline reason.
     */
    void onUserOffline(
        uid_t uid,
        [[maybe_unused]] USER_OFFLINE_REASON_TYPE reason
    ) override {
        if (stopping_)
            return;
        {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            video_publications_.erase(uid);
            if (uid != bot_uid_)
                return;
            clear_result_video();
            result_audio_enabled_ = false;
            bot_uid_ = 0;
        }
    }

    /**
     * Resolves the result account and applies its retained volume and known publication state.
     *
     * @param[in] uid The native numeric RTC user identifier used to identify and filter the result stream.
     * @param[in] info The native RTC user information linking an account to its UID.
     */
    void onUserInfoUpdated(
        uid_t uid,
        const rtc::UserInfo& info
    ) override {
        if (stopping_ || bot_ != info.userAccount)
            return;

        try {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            if (!engine_ || stopping_)
                return;

            check_rtc(
                engine_->adjustUserPlaybackSignalVolume(uid, int(std::lround(volume_ * 100))),
                "adjustUserPlaybackSignalVolume"
            );
            const auto previous_uid = bot_uid_.load();
            if (previous_uid != uid) {
                clear_result_video();
                update_result_audio(previous_uid, false);
                if (previous_uid)
                    check_rtc(engine_->muteRemoteVideoStream(previous_uid, true), "muteRemoteVideoStream");
            }
            bot_uid_ = uid;
            const auto published = video_publications_.find(uid);
            if (published != video_publications_.end())
                apply_video_publication(uid, published->second);
        } catch (const Error& error) {
            failure(error.what(), error.code);
        }
    }

    /** Reads publication flags before video subscription starts. */
    void onUserStateChanged(uid_t uid, REMOTE_USER_STATE state) override {
        const auto flags = static_cast<unsigned>(state);
        remote_video_publication(uid, (flags & USER_STATE_MUTE_VIDEO) == 0 &&
            (flags & USER_STATE_ENABLE_VIDEO) != 0 && (flags & USER_STATE_ENABLE_LOCAL_VIDEO) != 0);
    }

    /** Tracks explicit remote publication changes. */
    void onUserMuteVideo(uid_t uid, bool muted) override {
        remote_video_publication(uid, !muted);
    }

    /** Tracks whether the remote video module is enabled. */
    void onUserEnableVideo(uid_t uid, bool enabled) override {
        remote_video_publication(uid, enabled);
    }

    /** Tracks whether the remote source has video capture enabled. */
    void onUserEnableLocalVideo(uid_t uid, bool enabled) override {
        remote_video_publication(uid, enabled);
    }

    /** Handles remote video stops even when the result user remains in the room. */
    void onRemoteVideoStateChanged(uid_t uid, REMOTE_VIDEO_STATE, REMOTE_VIDEO_STATE_REASON reason, int) override {
        if (reason == REMOTE_VIDEO_STATE_REASON_REMOTE_OFFLINE)
            onUserOffline(uid, USER_OFFLINE_QUIT);
        else if (reason == REMOTE_VIDEO_STATE_REASON_REMOTE_MUTED)
            remote_video_publication(uid, false);
        else if (reason == REMOTE_VIDEO_STATE_REASON_REMOTE_UNMUTED)
            remote_video_publication(uid, true);
    }

    /** Remembers publication and subscribes only after the selected account is resolved. */
    void remote_video_publication(uid_t uid, bool published) {
        if (stopping_)
            return;
        try {
            std::lock_guard<std::mutex> lock(engine_mutex_);
            if (!engine_ || stopping_)
                return;
            video_publications_[uid] = published;
            if (uid == bot_uid_)
                apply_video_publication(uid, published);
        } catch (const Error& error) {
            failure(error.what(), error.code);
        }
    }

    /** Applies the selected user's publication under the engine mutex. */
    void apply_video_publication(uid_t uid, bool published) {
        bool subscribed;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            subscribed = result_video_subscribed_;
        }
        if (published == subscribed)
            return;
        if (!published) {
            clear_result_video();
            update_result_audio(uid, false);
            check_rtc(engine_->muteRemoteVideoStream(uid, true), "muteRemoteVideoStream");
            return;
        }
        check_rtc(engine_->muteRemoteVideoStream(uid, false), "muteRemoteVideoStream");
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            result_video_subscribed_ = true;
        }
        changed_.notify_all();
    }

    /** Invalidates queued and in-flight frames before exposing the unpublish event. */
    void clear_result_video() {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        if (!result_video_subscribed_)
            return;
        result_video_subscribed_ = false;
        ++result_video_revision_;
        if (std::exchange(generation_confirmed_, false)) {
            generation_result_lost_ = true;
            if (callbacks_.video_unpublished)
                callbacks_.video_unpublished();
            changed_.notify_all();
        }
        clear_downlink_statistics();
    }

    /** Clears the published result statistics before another generation can publish frames. */
    void clear_downlink_statistics() {
        if (callbacks_.statistics) {
            xmax_video_statistics value{};
            value.struct_size = sizeof(value);
            value.direction = 1;
            callbacks_.statistics(value);
        }
        if (callbacks_.event) {
            xmax_event event{};
            event.type = XMAX_EVENT_STATISTICS;
            event.direction = 1;
            event.quality = -1;
            event.width = event.height = event.fps = -1;
            callbacks_.event(event);
        }
    }

    /** Updates the current audio subscription under the engine mutex. */
    void update_result_audio(uid_t uid, bool enabled) {
        if (enabled == result_audio_enabled_)
            return;
        if (uid && engine_ && !stopping_)
            check_rtc(engine_->muteRemoteAudioStream(uid, !enabled), "muteRemoteAudioStream");
        result_audio_enabled_ = enabled;
    }

    /**
     * Converts capture, encoding, and send statistics to SDK uplink events and logs.
     *
     * @param[in] source_type The native video source type.
     * @param[in] stats The native RTC statistics snapshot, borrowed for the callback.
     */
    void onLocalVideoStats(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        const LocalVideoStats& stats
    ) override {
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
        if (callbacks_.statistics) {
            xmax_video_statistics value{};
            value.struct_size = sizeof(value);
            value.available = 1;
            value.direction = 0;
            value.width = stats.encodedFrameWidth;
            value.height = stats.encodedFrameHeight;
            value.capture_fps = stats.captureFrameRate;
            value.encode_fps = stats.encoderOutputFrameRate;
            value.send_fps = stats.sentFrameRate;
            value.decode_fps = value.render_fps = value.e2e_ms = -1;
            value.bitrate_kbps = stats.sentBitrate;
            value.packet_loss_percent = stats.txPacketLossRate;
            callbacks_.statistics(value);
        }
        RtcStatsLogger::video(
            e,
            stats.captureFrameRate,
            stats.encoderOutputFrameRate
        );

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

    /**
     * Converts decoding and rendering statistics to SDK downlink events and logs.
     *
     * @param[in] stats The native RTC statistics snapshot, borrowed for the callback.
     */
    void onRemoteVideoStats(
        const RemoteVideoStats& stats
    ) override {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        if (stopping_ || stats.uid != bot_uid_ || !result_video_subscribed_ || !generation_confirmed_)
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
        if (callbacks_.statistics) {
            xmax_video_statistics value{};
            value.struct_size = sizeof(value);
            value.available = 1;
            value.direction = 1;
            copy_utf8(
                value.user_id,
                sizeof(value.user_id),
                bot_
            );
            value.width = stats.width;
            value.height = stats.height;
            value.capture_fps = value.encode_fps = value.send_fps = -1;
            value.decode_fps = stats.decoderOutputFrameRate;
            value.render_fps = stats.rendererOutputFrameRate;
            value.bitrate_kbps = stats.receivedBitrate;
            value.packet_loss_percent = stats.packetLossRate;
            value.e2e_ms = stats.e2eDelay;
            callbacks_.statistics(value);
        }
        RtcStatsLogger::video(
            e,
            -1,
            stats.decoderOutputFrameRate
        );

        if (callbacks_.event)
            callbacks_.event(e);
    }

    /**
     * Forwards directional network quality to adaptation and diagnostic receivers.
     *
     * @param[in] uid The native numeric RTC user identifier used to identify and filter the result stream.
     * @param[in] up The native uplink network-quality value.
     * @param[in] down The native downlink network-quality value.
     */
    void onNetworkQuality(
        uid_t uid,
        int up,
        int down
    ) override {
        if (stopping_ || uid != 0 || !callbacks_.event)
            return;

        xmax_event sample{};
        sample.type = XMAX_EVENT_NETWORK_QUALITY;
        sample.quality = up;
        sample.code = down;
        callbacks_.event(sample);

        for (int direction = 0; direction < 2; ++direction) {
            xmax_event e{};
            e.type = XMAX_EVENT_STATISTICS;
            e.direction = direction;
            e.quality = direction ? down : up;
            e.width = e.height = e.fps = e.bitrate_kbps = e.packet_loss_percent = e.e2e_ms = -1;
            callbacks_.event(e);
        }
    }

    /**
     * Leaves native capture frames unchanged; SDK video is published through external input.
     *
     * @param[in] source_type The native video source type.
     * @param[in] video The borrowed native video frame, valid only during the callback. Copy pixels to
     * retain them.
     *
     * @return True to preserve native frame processing.
     */
    bool onCaptureVideoFrame(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        [[maybe_unused]] VideoFrame& video
    ) override {
        return true;
    }

    /**
     * Leaves native pre-encode frames unchanged.
     *
     * @param[in] source_type The native video source type.
     * @param[in] video The borrowed native video frame, valid only during the callback. Copy pixels to
     * retain them.
     *
     * @return True to preserve native frame processing.
     */
    bool onPreEncodeVideoFrame(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        [[maybe_unused]] VideoFrame& video
    ) override {
        return true;
    }

    /**
     * Leaves player frames unchanged and does not deliver them as generation results.
     *
     * @param[in] video The borrowed native video frame, valid only during the callback. Copy pixels to
     * retain them.
     * @param[in] player_id The native media player identifier.
     *
     * @return True to preserve native frame processing.
     */
    bool onMediaPlayerVideoFrame(
        [[maybe_unused]] VideoFrame& video,
        [[maybe_unused]] int player_id
    ) override {
        return true;
    }

    /**
     * Leaves transcoded frames unchanged and does not deliver them as generation results.
     *
     * @param[in] video The borrowed native video frame, valid only during the callback. Copy pixels to
     * retain them.
     *
     * @return True to preserve native frame processing.
     */
    bool onTranscodedVideoFrame(
        [[maybe_unused]] VideoFrame& video
    ) override {
        return true;
    }

    /**
     * Preserves local capture; local microphone PCM is excluded from remote output.
     *
     * @param[in] channel The callback room identifier.
     * @param[in,out] frame The borrowed PCM, left unchanged.
     *
     * @return True to preserve the native audio pipeline.
     */
    bool onRecordAudioFrame(
        [[maybe_unused]] const char* channel,
        [[maybe_unused]] NativeAudioFrame& frame
    ) override {
        return true;
    }

    /**
     * Preserves mixed playback; output uses only result-user PCM before mixing.
     *
     * @param[in] channel The callback room identifier.
     * @param[in,out] frame The borrowed PCM, left unchanged.
     *
     * @return True to preserve the native audio pipeline.
     */
    bool onPlaybackAudioFrame(
        [[maybe_unused]] const char* channel,
        [[maybe_unused]] NativeAudioFrame& frame
    ) override {
        return true;
    }

    /**
     * Preserves mixed local and remote audio without copying it to application output.
     *
     * @param[in] channel The callback room identifier.
     * @param[in,out] frame The borrowed PCM, left unchanged.
     *
     * @return True to preserve the native audio pipeline.
     */
    bool onMixedAudioFrame(
        [[maybe_unused]] const char* channel,
        [[maybe_unused]] NativeAudioFrame& frame
    ) override {
        return true;
    }

    /**
     * Preserves ear monitoring without observing local audio.
     *
     * @param[in,out] frame The borrowed PCM, left unchanged.
     *
     * @return True to preserve the native audio pipeline.
     */
    bool onEarMonitoringAudioFrame(
        [[maybe_unused]] NativeAudioFrame& frame
    ) override {
        return true;
    }

    /**
     * Supplies read-only 10 ms, 48 kHz mono parameters for an unobserved native audio stage.
     *
     * @return The native PCM configuration; only the before-mixing position is enabled.
     */
    NativeAudioParams getRecordAudioParams() override {
        return {48000, 1, RAW_AUDIO_FRAME_OP_MODE_READ_ONLY, 480};
    }

    /**
     * Supplies read-only 10 ms, 48 kHz mono parameters for an unobserved native audio stage.
     *
     * @return The native PCM configuration; only the before-mixing position is enabled.
     */
    NativeAudioParams getPlaybackAudioParams() override {
        return {48000, 1, RAW_AUDIO_FRAME_OP_MODE_READ_ONLY, 480};
    }

    /**
     * Supplies read-only 10 ms, 48 kHz mono parameters for an unobserved native audio stage.
     *
     * @return The native PCM configuration; only the before-mixing position is enabled.
     */
    NativeAudioParams getMixedAudioParams() override {
        return {48000, 1, RAW_AUDIO_FRAME_OP_MODE_READ_ONLY, 480};
    }

    /**
     * Supplies read-only 10 ms, 48 kHz mono parameters for an unobserved native audio stage.
     *
     * @return The native PCM configuration; only the before-mixing position is enabled.
     */
    NativeAudioParams getEarMonitoringAudioParams() override {
        return {48000, 1, RAW_AUDIO_FRAME_OP_MODE_READ_ONLY, 480};
    }

    /**
     * Selects individual remote-user PCM before remote-user mixing.
     *
     * @return The before-mixing audio observation flag.
     */
    int getObservedAudioFramePosition() override {
        return agora::media::IAudioFrameObserverBase::AUDIO_FRAME_POSITION_BEFORE_MIXING;
    }

    /**
     * Copies current result-user PCM before native storage is reused, preserving normal playback.
     *
     * @param[in] channel The room identifier used to exclude packets from other connections.
     * @param[in] uid The remote UID, filtered against the service result account.
     * @param[in,out] native The borrowed native PCM, never modified by observation.
     *
     * @return True to preserve native playback even when observation discards invalid data.
     */
    bool onPlaybackAudioFrameBeforeMixing(
        const char* channel,
        uid_t uid,
        NativeAudioFrame& native
    ) override {
        if (stopping_ || !audio_observing_ || !result_audio_enabled_ || !channel || room_ != channel || bot_uid_ == 0 ||
            uid != bot_uid_ || !callbacks_.audio)
            return true;

        uint64_t revision;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            if (!result_video_subscribed_ || !generation_confirmed_)
                return true;
            revision = result_video_revision_;
        }
        const auto deliver = [&](xmax::detail::AudioFrame frame) {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            if (revision == result_video_revision_ && !stopping_ && result_audio_enabled_ && uid == bot_uid_)
                callbacks_.audio(std::move(frame));
        };
        try {
            xmax::detail::AudioFrame frame;
            frame.sample_rate = uint32_t(native.samplesPerSec);
            frame.channels = uint32_t(native.channels);
            frame.samples_per_channel = uint32_t(native.samplesPerChannel);
            frame.render_time_ms = native.renderTimeMs;
            if (native.buffer && native.bytesPerSample == TWO_BYTES_PER_SAMPLE &&
                native.samplesPerSec >= 8000 && native.samplesPerSec <= 192000 && native.channels >= 1 &&
                native.channels <= 2 && native.samplesPerChannel >= 1 && native.samplesPerChannel <= 192000) {
                const auto size = size_t(native.samplesPerChannel) * native.channels * 2;
                const auto* bytes = static_cast<const uint8_t*>(native.buffer);
                frame.data.assign(
                    bytes,
                    bytes + size
                );
            }
            deliver(std::move(frame));
        } catch (...) {
            // Invalid or unavailable PCM must break output continuity without interrupting native playback.
            try {
                deliver({});
            } catch (...) {
            }
        }
        return true;
    }

    /**
     * Requests native result frames in BGRA format.
     *
     * @return VIDEO_PIXEL_BGRA.
     */
    media_base::VIDEO_PIXEL_FORMAT getVideoFormatPreference() override {
        return media_base::VIDEO_PIXEL_BGRA;
    }

    /**
     * Observes result frames before native rendering.
     *
     * @return The pre-renderer frame-observation flag.
     */
    uint32_t getObservedFramePosition() override {
        return media_base::POSITION_PRE_RENDERER;
    }

    /**
     * Requests that the native engine apply rotation to pixels.
     *
     * @return True to have the native engine apply pixel rotation.
     */
    bool getRotationApplied() override {
        return true;
    }

    /**
     * Requests unmirrored result pixels.
     *
     * @return False to disable native pixel mirroring.
     */
    bool getMirrorApplied() override {
        return false;
    }

    /**
     * Copies and delivers the result user's pixels, satisfying first-result readiness.
     *
     * @param[in] channel The RTC room name that triggered the callback.
     * @param[in] uid The native numeric RTC user identifier used to identify and filter the result stream.
     * @param[in] video The borrowed native video frame, valid only during the callback. Copy pixels to
     * retain them.
     *
     * @return True to preserve native rendering; valid result frames are also copied and delivered
     * separately.
     */
    bool onRenderVideoFrame(
        const char* channel,
        uid_t uid,
        VideoFrame& video
    ) override {
        if (stopping_ || !channel || room_ != channel || bot_uid_ == 0 || uid != bot_uid_)
            return true;
        uint64_t revision;
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            if (!generation_confirmed_ || !result_video_subscribed_)
                return true;
            revision = result_video_revision_;
        }
        try {
            require(
                video.type == media_base::VIDEO_PIXEL_BGRA && video.width > 0 && video.height > 0 &&
                    video.yStride > 0,
                XMAX_RTC_ERROR,
                "Unsupported decoded video format"
            );

            xmax_video_frame v{};
            v.struct_size = sizeof(v);
            v.pixel_format = XMAX_BGRA;
            v.width = uint32_t(video.width);
            v.height = uint32_t(video.height);
            v.timestamp_us = std::max<int64_t>(0, video.renderTimeMs) * 1000;
            v.planes[0] = video.yBuffer;

            size_t stride = size_t(video.yStride);
            const size_t row_bytes = size_t(video.width) * 4;

            // Accept native stride layouts expressed as total contiguous bytes or bytes per row.
            if (stride == row_bytes * size_t(video.height))
                stride = row_bytes;
            else if (stride >= size_t(video.width) && stride < row_bytes)
                stride *= 4;

            v.strides[0] = stride;
            v.plane_sizes[0] = stride * size_t(video.height);

            auto frame = Frame::copy(v);
            frame.color_primaries = uint8_t(video.colorSpace.primaries);
            frame.color_transfer = uint8_t(video.colorSpace.transfer);

            // Treat RTC video as opaque; decoders may leave BGRA alpha at zero.
            for (size_t i = 3; i < frame.planes[0].size(); i += 4)
                frame.planes[0][i] = 255;

            {
                std::lock_guard<std::mutex> lock(metadata_mutex_);
                if (revision != result_video_revision_ || stopping_ || uid != bot_uid_)
                    return true;
                if (callbacks_.frame)
                    callbacks_.frame(std::move(frame));
            }
        } catch (const std::exception&) {
            failure("Cannot copy a decoded RTC video frame");
        }
        return true;
    }
};
} // namespace

/**
 * Reports whether the current build includes a native RTC backend.
 */
bool rtc_available() noexcept {
    return true;
}

/**
 * Creates a native RTC adapter or reports unsupported capability on other platforms.
 *
 * @return A shared native RTC adapter; unsupported platforms throw a capability error.
 */
std::shared_ptr<Rtc> make_rtc() {
    return std::make_shared<AgoraRtc>();
}
} // namespace xmax::detail
