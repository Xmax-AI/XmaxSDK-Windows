//
//  ExternalVideoController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/External/ExternalVideoController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Service/Media/MediaService.hpp"

namespace xmax::detail {

/**
 * Validates and binds an input format, resetting timestamp tracking for the binding.
 *
 * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
 * @param[in] binding The source revision; zero reserves the channel for unversioned camera delivery.
 * @param[in] has_audio Whether this external source supplies PCM instead of microphone input.
 */
void ExternalVideoController::prepare(
    xmax_video_format format,
    uint64_t binding,
    bool has_audio
) {
    MediaService::input(format);
    require(
        format.width >= 4 && format.height >= 4,
        XMAX_INVALID_ARGUMENT,
        "External input must be at least 4 by 4 pixels"
    );

    std::lock_guard<std::mutex> lock(mutex_);
    require(
        !requesting_,
        XMAX_INVALID_STATE,
        "Stop delivery before replacing an external source"
    );
    format_ = format;
    binding_ = binding;
    has_audio_ = has_audio;
    audio_converter_ = {};
    last_timestamp_ = -1;
    XmaxLogger::write(
        LogCategory::Media,
        XMAX_LOG_INFO,
        [&](auto language) {
            return XmaxLogger::localized(
                       language,
                       "外部视频已绑定 (External Video Bound)",
                       "External Video Bound"
                   ) +
                   "\n└─ " + std::to_string(format.width) + " × " + std::to_string(format.height) + " @ " +
                   std::to_string(format.fps) + " fps";
        }
    );
}

/**
 * Installs a publication receiver and starts requesting input frames.
 *
 * @param[in] sink The synchronous frame receiver, invoked on the submitting media thread.
 * @param[in] audio The synchronous receiver for converted audio packets.
 */
void ExternalVideoController::start(
    VideoFrameSink sink,
    ExternalAudioConverter::Receiver audio
) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(
        format_.width != 0,
        XMAX_INVALID_STATE,
        "No external input is bound"
    );
    require(
        bool(sink),
        XMAX_INVALID_ARGUMENT,
        "An external frame sink is required"
    );
    sink_ = std::move(sink);
    audio_ = std::move(audio);
    audio_converter_.reset();
    requesting_ = true;
}

/**
 * Stops publication delivery while retaining the input format and local preview.
 */
void ExternalVideoController::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    requesting_ = false;
    sink_ = {};
    audio_ = {};
    audio_converter_.reset();
}

/**
 * Releases the input binding, clearing format, publication state, and timestamp tracking.
 */
void ExternalVideoController::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    requesting_ = false;
    sink_ = {};
    format_ = {};
    audio_ = {};
    has_audio_ = false;
    audio_converter_ = {};
    binding_ = 0;
    last_timestamp_ = -1;
}

/**
 * Reports whether a valid external input format is bound.
 */
bool ExternalVideoController::bound() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_.width != 0;
}

/**
 * Returns the input format under the binding lock.
 *
 * @return The bound input format; dimensions and frame rate are 0 when unbound.
 */
xmax_video_format ExternalVideoController::format() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return format_;
}

/**
 * Validates and copies a borrowed external frame, delivering it synchronously to publication and preview
 * receivers.
 *
 * @param[in] source The frame or borrowed view to read, with a valid pixel layout.
 * @param[in] binding The source revision; zero uses the current legacy binding. Stale revisions are ignored.
 */
void ExternalVideoController::push(
    const xmax_video_frame& source,
    uint64_t binding
) {
    // Stop shares this lock so in-flight frame delivery finishes before it returns.
    std::lock_guard<std::mutex> lock(mutex_);
    if (binding != 0 && binding != binding_)
        return;

    require(
        format_.width != 0,
        XMAX_INVALID_STATE,
        "No external input is bound"
    );
    require(
        source.struct_size >= sizeof(source),
        XMAX_INVALID_ARGUMENT,
        "External video frame structure is too small"
    );
    if (binding != 0 && (source.timestamp_us < 0 || source.timestamp_us <= last_timestamp_))
        return;
    require(
        source.width == format_.width && source.height == format_.height,
        XMAX_INVALID_ARGUMENT,
        "External frame does not match the negotiated format"
    );
    require(
        source.timestamp_us > last_timestamp_,
        XMAX_INVALID_ARGUMENT,
        "External frame timestamps must increase strictly"
    );

    auto frame = Frame::copy(source);
    last_timestamp_ = source.timestamp_us;

    if (requesting_ && sink_)
        sink_(frame);

    if (preview_)
        preview_(std::move(frame));
}

/**
 * Replaces the local preview receiver, invoked on the submitting media thread.
 *
 * @param[in] sink The synchronous frame receiver, invoked on the submitting media thread.
 * @param[in] binding A nonzero replacement revision invalidates previous sources before preview changes.
 */
void ExternalVideoController::preview(
    std::function<void(Frame)> sink,
    uint64_t binding
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (binding)
        binding_ = binding;
    preview_ = std::move(sink);
}

/**
 * Delivers camera frames at capture dimensions; RTC handles encoding dimensions and aspect ratio.
 *
 * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
 * synchronous call returns.
 */
void ExternalVideoController::push_owned(
    Frame frame
) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(
        format_.width != 0,
        XMAX_INVALID_STATE,
        "No camera input is bound"
    );
    validate_frame(frame.view());
    require(
        frame.timestamp_us > last_timestamp_,
        XMAX_INVALID_ARGUMENT,
        "Capture timestamps must increase"
    );
    last_timestamp_ = frame.timestamp_us;

    if (requesting_ && sink_)
        sink_(frame);

    if (preview_)
        preview_(std::move(frame));
}

/**
 * Reports whether this source declares external PCM upload.
 */
bool ExternalVideoController::has_audio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_audio_;
}

/**
 * Converts source-bound PCM only while publication is active, without queuing for reconnection.
 *
 * @param[in] frame The borrowed interleaved PCM samples and source timestamp.
 * @param[in] binding The source revision; stale or audio-disabled bindings ignore input.
 */
void ExternalVideoController::push_audio(
    const xmax_external_audio_frame& frame,
    uint64_t binding
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (binding_ != binding || !has_audio_)
        return;
    if (requesting_ && audio_) {
        audio_converter_.append(
            frame,
            audio_
        );
    } else {
        audio_converter_.append(
            frame,
            [](const ExternalAudioPacket&) {}
        );
        audio_converter_.reset();
    }
}
} // namespace xmax::detail
