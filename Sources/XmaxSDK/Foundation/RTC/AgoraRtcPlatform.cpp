//
//  AgoraRtcPlatform.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Logging/RtcStatsLogger.hpp"
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

/**
 * 保护进程级 RTC 引擎独占标记的互斥锁。
 */
std::mutex engine_lease;

/**
 * 进程内是否已有活动 RTC 引擎适配器。
 */
bool engine_in_use = false;

/**
 * 将负数原生 RTC 结果转换为保留原始错误码的 SDK 异常。
 *
 * @param[in] code 原生 RTC 操作结果；0 表示成功，非零值转换为 SDK 错误。
 * @param[in] operation 当前原生 RTC 操作的诊断名称。
 */
void check_rtc(
    int code,
    const char* operation
) {
    if (code < 0)
        throw Error(XMAX_RTC_ERROR, std::string(operation) + " failed (" + std::to_string(code) + ")");
}

/**
 * 管理 RTC 引擎、外部视频发布和结果帧观察；进程内同时只允许一个引擎实例。
 */
class AgoraRtc final : public Rtc, private IRtcEngineEventHandler, private IVideoFrameObserver {

    /**
     * 本适配器是否持有进程级 RTC 引擎独占租约。
     */
    bool owns_lease_ = false;

    /**
     * 保护原生 RTC 引擎调用及引擎生命周期的互斥锁。
     */
    std::mutex engine_mutex_;

    /**
     * 保护房间加入、结果首帧及错误元数据的互斥锁。
     */
    std::mutex metadata_mutex_;

    /**
     * 通知房间加入、结果首帧或 RTC 错误已发生的条件变量。
     */
    std::condition_variable changed_;

    /**
     * 本适配器拥有的原生 RTC 引擎指针。
     */
    IRtcEngine* engine_ = nullptr;

    /**
     * 借用当前引擎的媒体接口，用于外部视频帧推送。
     */
    IMediaEngine* media_ = nullptr;

    /**
     * RTC 是否正在停止，回调需据此拒绝继续交付。
     */
    std::atomic<bool> stopping_{false};

    /**
     * RTC 是否已成功加入当前房间。
     */
    std::atomic<bool> joined_{false};

    /**
     * 当前生成是否已收到结果首帧。
     */
    bool first_frame_ = false;

    /**
     * 当前或下一次连接使用的远端播放音量，范围为 [0, 1]。
     */
    float volume_ = 0;

    /**
     * 经过凭证脱敏的最近一次 RTC 失败原因，供等待者及错误回调读取。
     */
    std::string failure_;

    /**
     * 当前原生错误对应的 SDK 分类，由元数据锁保护。
     */
    xmax_result failure_code_ = XMAX_RTC_ERROR;

    /**
     * 当前 RTC 房间标识。
     */
    std::string room_;

    /**
     * 服务端结果用户的账号标识。
     */
    std::string bot_;

    /**
     * 已解析的结果服务用户 UID，用于过滤结果帧及统计。
     */
    std::atomic<uid_t> bot_uid_{0};

    /**
     * 原生可靠数据流标识，用于发送房间信令。
     */
    int stream_id_ = 0;

    /**
     * 当前连接的帧、事件、错误和发布通知回调。
     */
    RtcCallbacks callbacks_;

    /**
     * 为多平面输入临时拼接的连续视频字节缓冲区。
     */
    std::vector<uint8_t> packed_;

    /**
     * 媒体单调时间到原生 RTC 单调时钟的毫秒偏移，首帧推送时建立。
     */
    std::optional<int64_t> timestamp_offset_ms_;

    /**
     * 保持当前 RTC 凭证在日志脱敏集合中注册的保护对象。
     */
    std::shared_ptr<const std::string> log_token_;

    /**
     * 等待房间加入或结果首帧，期间响应取消并执行保活。
     *
     * @param[in] first true 等待结果首帧，false 等待 RTC 加入房间。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] keepalive 等待期间周期执行的保活函数，不得持有跨线程生命周期锁。
     */
    void wait(
        bool first,
        const Cancel& cancel,
        const std::function<void()>& keepalive = [] {}
    ) {
        const auto deadline = Clock::now() + std::chrono::seconds(first ? 30 : 15);
        std::unique_lock<std::mutex> lock(metadata_mutex_);
        while (!(first ? first_frame_ : joined_.load())) {
            lock.unlock();
            keepalive();
            lock.lock();
            require(!cancel(), XMAX_CANCELLED, "RTC operation cancelled");
            require(failure_.empty(), failure_code_, failure_);
            require(
                Clock::now() < deadline, XMAX_TIMEOUT,
                first ? "Timed out waiting for generated video" : "RTC join timed out"
            );
            changed_.wait_for(lock, std::chrono::milliseconds(20));
        }
    }

    /**
     * 保存脱敏后的 RTC 错误，唤醒等待者并通知内部错误接收器。
     *
     * @param[in] message 以 NUL 结尾的 RTC 失败原因，保存和交付前执行凭证脱敏。
     * @param[in] code 原生错误对应的 SDK 分类。
     */
    void failure(
        const char* message,
        xmax_result code = XMAX_RTC_ERROR
    ) {
        if (stopping_)
            return;

        const auto safe = XmaxLogger::redact(message);
        XmaxLogger::write(LogCategory::RTC, XMAX_LOG_ERROR, [&](auto language) {
            return XmaxLogger::localized(language, "RTC 失败 (RTC Failure)", "RTC Failure") + "\n└─ " + safe;
        });
        {
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            failure_ = safe;
            failure_code_ = code;
        }
        changed_.notify_all();

        if (callbacks_.failure)
            callbacks_.failure(Error(code, safe));
    }

  public:

    /**
     * 取得进程级 RTC 引擎租约，在服务端会话分配前拒绝引擎冲突。
     */
    AgoraRtc() {
        std::lock_guard<std::mutex> lock(engine_lease);
        require(
            !engine_in_use, XMAX_INVALID_STATE, "Another Xmax RTC session owns the process-wide Agora engine"
        );
        engine_in_use = true;
        owns_lease_ = true;
    }

    /**
     * 离开 RTC 并释放进程级引擎租约。
     */
    ~AgoraRtc() override {
        leave();
    }

    /**
     * 初始化 RTC 引擎、配置外部视频与音频策略，并等待成功加入房间。
     *
     * @param[in] c 包含应用、房间、用户、令牌和结果服务账号的已验证连接配置。
     * @param[in] encoding 已解析的编码尺寸、帧率、码率和降级策略。
     * @param[in] mic 是否在加入 RTC 房间时启动麦克风采集。
     * @param[in] cb 接收结果帧、RTC 事件、错误和首次发布通知的回调；停止后不再触发。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void join(
        const Connection& c,
        VideoEncodingConfiguration encoding,
        bool mic,
        RtcCallbacks cb,
        const Cancel& cancel
    ) override {
        log_token_ = XmaxLogger::protect_secret(c.token);
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
        check_rtc(
            engine_->queryInterface(AGORA_IID_MEDIA_ENGINE, reinterpret_cast<void**>(&media_)),
            "query media engine"
        );
        check_rtc(media_->setExternalVideoSource(true, false), "setExternalVideoSource");
        check_rtc(media_->registerVideoFrameObserver(this), "registerVideoFrameObserver");

        VideoEncoderConfiguration encoder;
        encoder.dimensions = VideoDimensions(int(f.width), int(f.height));
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
            "joinChannelWithUserAccount"
        );
        wait(false, cancel);

        DataStreamConfig stream;
        stream.ordered = true;
        stream.syncWithAudio = false;
        check_rtc(engine_->createDataStream(&stream_id_, stream), "createDataStream");
    }

    /**
     * 发送一个房间信令分片，传递原生传输错误。
     *
     * @param[in] bytes 待处理的编码字节，本次同步调用返回前必须有效。
     */
    void send(
        const std::string& bytes
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(engine_ && joined_ && !stopping_, XMAX_RTC_ERROR, "RTC room is not joined");
        check_rtc(engine_->sendStreamMessage(stream_id_, bytes.data(), bytes.size()), "sendStreamMessage");
    }

    /**
     * 封装已拥有的视频像素，使用原生单调时钟偏移发布外部帧。
     *
     * @param[in] f 待校验或交付的视频帧，调用期间其像素存储必须有效。
     */
    void push(
        const Frame& f
    ) override {
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

    /**
     * 等待真实结果流首帧，同时响应取消并执行保活。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] keepalive 等待期间周期执行的保活函数，不得持有跨线程生命周期锁。
     */
    void wait_remote(
        const Cancel& cancel,
        const std::function<void()>& keepalive
    ) override {
        wait(true, cancel, keepalive);
    }

    /**
     * 为下一次生成清除结果首帧等待标记。
     */
    void begin_generation() override {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        first_frame_ = false;
    }

    /**
     * 更新当前引擎和后续用户加入时使用的远端播放音量。
     *
     * @param[in] volume 范围为 [0, 1] 的有限音量值，0 表示静音。
     */
    void volume(
        float volume
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        if (engine_ && !stopping_)
            check_rtc(engine_->adjustPlaybackSignalVolume(int(volume * 100)), "adjustPlaybackSignalVolume");
        volume_ = volume;
    }

    /**
     * 应用生效的视频尺寸、帧率、码率限制和降级偏好。
     *
     * @param[in] value 已解析的编码尺寸、帧率、码率限制与降级偏好。
     */
    void configure_encoding(
        VideoEncodingConfiguration value
    ) override {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        require(engine_ && !stopping_, XMAX_INVALID_STATE, "RTC is not connected");

        VideoEncoderConfiguration encoder;
        encoder.dimensions = VideoDimensions(int(value.format.width), int(value.format.height));
        encoder.frameRate = int(value.format.fps);
        encoder.orientationMode = value.format.width >= value.format.height ? ORIENTATION_MODE_FIXED_LANDSCAPE
                                                                            : ORIENTATION_MODE_FIXED_PORTRAIT;
        encoder.minBitrate = value.minimum_bitrate;
        encoder.bitrate = value.maximum_bitrate;
        encoder.degradationPreference =
            value.preference == XMAX_ENCODER_AUTO
                ? MAINTAIN_AUTO
                : (value.preference == XMAX_ENCODER_MAINTAIN_QUALITY ? MAINTAIN_QUALITY : MAINTAIN_FRAMERATE);
        encoder.mirrorMode = VIDEO_MIRROR_MODE_DISABLED;
        check_rtc(engine_->setVideoEncoderConfiguration(encoder), "setVideoEncoderConfiguration");
    }

    /**
     * 停止发布，在回调锁之外释放引擎，并等待回调交付排空。
     */
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

        // 释放引擎期间会等待回调线程结束，此处不能持有应用互斥锁。
        if (media) {
            media->registerVideoFrameObserver(nullptr);
            media->release();
        }

        if (engine) {
            engine->leaveChannel();
            engine->release();
        }

        // 最后一个共享引用可能由媒体线程释放，因此引擎租约不持有跨生命周期的线程互斥锁。
        std::lock_guard<std::mutex> lock(engine_lease);

        if (owns_lease_) {
            engine_in_use = false;
            owns_lease_ = false;
        }
    }

  private:

    /**
     * 记录成功加入 RTC 房间，并唤醒加入等待者。
     *
     * @param[in] channel 触发回调的 RTC 房间名称。
     * @param[in] uid 原生 RTC 用户数值标识，用于结果流识别与过滤。
     * @param[in] elapsed 原生 SDK 报告的操作耗时，单位为毫秒。
     */
    void onJoinChannelSuccess(
        [[maybe_unused]] const char* channel,
        [[maybe_unused]] uid_t uid,
        [[maybe_unused]] int elapsed
    ) override {
        joined_ = true;
        changed_.notify_all();
    }

    /**
     * 保留原生 RTC 错误码与描述，并通过 SDK 错误通道报告。
     *
     * @param[in] code 原生 RTC 回调报告的错误码，诊断中保留其数值。
     * @param[in] description 原生错误描述；为空指针时使用缺省诊断。
     */
    void onError(
        int code,
        const char* description
    ) override {
        const auto message = RtcStatsLogger::error_message(code, description);
        failure(message.c_str());
    }

    /**
     * 通过 SDK 错误通道报告 RTC 连接丢失。
     */
    void onConnectionLost() override {
        failure("RTC connection was lost");
    }

    /**
     * 在原生引擎确认首帧已发布时记录启动时序。
     *
     * @param[in] source_type 原生视频源类型。
     * @param[in] elapsed 原生 SDK 报告的操作耗时，单位为毫秒。
     */
    void onFirstLocalVideoFramePublished(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        [[maybe_unused]] int elapsed
    ) override {
        if (!stopping_ && callbacks_.published)
            callbacks_.published();
    }

    /**
     * 识别结果服务用户并应用已保存的播放音量。
     *
     * @param[in] uid 原生 RTC 用户数值标识，用于结果流识别与过滤。
     * @param[in] elapsed 原生 SDK 报告的操作耗时，单位为毫秒。
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
                result = engine_->getUserInfoByUid(uid, &info);
        }

        if (result == 0)
            onUserInfoUpdated(uid, info);
    }

    /**
     * 将结果服务账号解析为 UID，用于过滤结果帧和统计信息。
     *
     * @param[in] uid 原生 RTC 用户数值标识，用于结果流识别与过滤。
     * @param[in] info 原生 RTC 用户信息，用于账号与 UID 关联。
     */
    void onUserInfoUpdated(
        uid_t uid,
        const rtc::UserInfo& info
    ) override {
        if (stopping_ || bot_ != info.userAccount)
            return;

        bot_uid_ = uid;

        std::lock_guard<std::mutex> lock(engine_mutex_);

        if (engine_ && !stopping_)
            engine_->muteRemoteAudioStream(uid, false);
    }

    /**
     * 在原生麦克风采集失败时报告 RTC 错误。
     *
     * @param[in] state 当前原生媒体状态或与回调共享的采集状态。
     * @param[in] reason 原生媒体状态变化原因或经过脱敏的传输错误原因。
     */
    void onLocalAudioStateChanged(
        LOCAL_AUDIO_STREAM_STATE state,
        LOCAL_AUDIO_STREAM_REASON reason
    ) override {
        if (!stopping_ && state == LOCAL_AUDIO_STREAM_STATE_FAILED)
            failure(
                "Microphone capture failed; check device access and permissions",
                reason == LOCAL_AUDIO_STREAM_REASON_DEVICE_NO_PERMISSION ? XMAX_MICROPHONE_PERMISSION_DENIED
                                                                         : XMAX_RTC_ERROR
            );
    }

    /**
     * 将实际采集、编码和发送统计转换为 SDK 上行事件及日志。
     *
     * @param[in] source_type 原生视频源类型。
     * @param[in] stats 原生 RTC 统计快照，仅在回调期间借用。
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
        RtcStatsLogger::video(e, stats.captureFrameRate, stats.encoderOutputFrameRate);

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
     * 将实际解码与渲染统计转换为 SDK 下行事件及日志。
     *
     * @param[in] stats 原生 RTC 统计快照，仅在回调期间借用。
     */
    void onRemoteVideoStats(
        const RemoteVideoStats& stats
    ) override {
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
        RtcStatsLogger::video(e, -1, stats.decoderOutputFrameRate);

        if (callbacks_.event)
            callbacks_.event(e);
    }

    /**
     * 向自适应策略和诊断接收器转发双向网络质量。
     *
     * @param[in] uid 原生 RTC 用户数值标识，用于结果流识别与过滤。
     * @param[in] up 上行原生网络质量枚举值。
     * @param[in] down 下行原生网络质量枚举值。
     */
    void onNetworkQuality(
        uid_t uid,
        int up,
        int down
    ) override {
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

    /**
     * 保持原生采集帧不变；SDK 视频通过外部输入路径发布。
     *
     * @param[in] source_type 原生视频源类型。
     * @param[in] video 原生视频帧，仅在回调期间借用；需保留时必须复制像素。
     * @return true，保留当前原生帧处理流程。
     */
    bool onCaptureVideoFrame(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        [[maybe_unused]] VideoFrame& video
    ) override {
        return true;
    }

    /**
     * 保持原生预编码帧不变。
     *
     * @param[in] source_type 原生视频源类型。
     * @param[in] video 原生视频帧，仅在回调期间借用；需保留时必须复制像素。
     * @return true，保留当前原生帧处理流程。
     */
    bool onPreEncodeVideoFrame(
        [[maybe_unused]] VIDEO_SOURCE_TYPE source_type,
        [[maybe_unused]] VideoFrame& video
    ) override {
        return true;
    }

    /**
     * 保持播放器视频帧不变，不将其作为生成结果交付。
     *
     * @param[in] video 原生视频帧，仅在回调期间借用；需保留时必须复制像素。
     * @param[in] player_id 原生媒体播放器标识。
     * @return true，保留当前原生帧处理流程。
     */
    bool onMediaPlayerVideoFrame(
        [[maybe_unused]] VideoFrame& video,
        [[maybe_unused]] int player_id
    ) override {
        return true;
    }

    /**
     * 保持转码视频帧不变，不将其作为生成结果交付。
     *
     * @param[in] video 原生视频帧，仅在回调期间借用；需保留时必须复制像素。
     * @return true，保留当前原生帧处理流程。
     */
    bool onTranscodedVideoFrame(
        [[maybe_unused]] VideoFrame& video
    ) override {
        return true;
    }

    /**
     * 请求以 BGRA 像素格式接收原生结果帧。
     *
     * @return VIDEO_PIXEL_BGRA。
     */
    media_base::VIDEO_PIXEL_FORMAT getVideoFormatPreference() override {
        return media_base::VIDEO_PIXEL_BGRA;
    }

    /**
     * 指定在原生渲染呈现之前观察结果帧。
     *
     * @return 原生渲染前视频帧观察位置的标记。
     */
    uint32_t getObservedFramePosition() override {
        return media_base::POSITION_PRE_RENDERER;
    }

    /**
     * 请求原生引擎将旋转应用到像素。
     *
     * @return true，表示由原生引擎将旋转应用到像素。
     */
    bool getRotationApplied() override {
        return true;
    }

    /**
     * 保持结果像素不做镜像。
     *
     * @return false，表示不请求原生镜像处理。
     */
    bool getMirrorApplied() override {
        return false;
    }

    /**
     * 复制结果服务用户的像素并交付结果帧，满足首帧就绪条件。
     *
     * @param[in] channel 触发回调的 RTC 房间名称。
     * @param[in] uid 原生 RTC 用户数值标识，用于结果流识别与过滤。
     * @param[in] video 原生视频帧，仅在回调期间借用；需保留时必须复制像素。
     * @return true，保留原生渲染流程；有效结果帧会另外复制交付。
     */
    bool onRenderVideoFrame(
        const char* channel,
        uid_t uid,
        VideoFrame& video
    ) override {
        if (stopping_ || !channel || room_ != channel || bot_uid_ == 0 || uid != bot_uid_)
            return true;
        try {
            require(
                video.type == media_base::VIDEO_PIXEL_BGRA && video.width > 0 && video.height > 0 &&
                    video.yStride > 0,
                XMAX_RTC_ERROR, "Unsupported decoded video format"
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

            // 兼容原生帧将 stride 表示为连续总字节数或每行字节数的布局。
            if (stride == row_bytes * size_t(video.height))
                stride = row_bytes;
            else if (stride >= size_t(video.width) && stride < row_bytes)
                stride *= 4;

            v.strides[0] = stride;
            v.plane_sizes[0] = stride * size_t(video.height);

            auto frame = Frame::copy(v);

            // RTC 视频按不透明画面呈现，统一设置解码器可能置零的 BGRA 透明通道。
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

/**
 * 检查当前构建是否包含原生 RTC 后端。
 */
bool rtc_available() noexcept {
    return true;
}

/**
 * 创建原生 RTC 适配器，不支持的平台返回明确的能力错误。
 *
 * @return 可共享的原生 RTC 适配器；平台不支持时抛出能力错误。
 */
std::shared_ptr<Rtc> make_rtc() {
    return std::make_shared<AgoraRtc>();
}
} // namespace xmax::detail
