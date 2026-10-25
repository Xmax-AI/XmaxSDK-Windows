//
//  RemoteMediaFrame.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/AudioFrame.hpp"
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

/**
 * Owns one remote packet with a result binding identity and a resolved presentation timeline.
 */
struct RemoteMediaFrame {

    /**
     * The packet content variant.
     */
    xmax_media_type type = XMAX_MEDIA_AUDIO;

    /**
     * The opaque identity shared by both tracks of one active result binding.
     */
    uint64_t stream_id = 0;

    /**
     * Whether a new run, format change, clock jump, or discarded output precedes this packet.
     */
    bool discontinuous = true;

    /**
     * The packet's time on the common receiver rendering timeline.
     */
    xmax_media_time presentation_time{};

    /**
     * The exact PCM duration, or an unavailable duration for video.
     */
    xmax_media_time duration{};

    /**
     * The owned PCM content when type is XMAX_MEDIA_AUDIO.
     */
    AudioFrame audio;

    /**
     * The owned read-only video content when type is XMAX_MEDIA_VIDEO.
     */
    std::shared_ptr<Frame> video;

    /**
     * Builds a borrowed pixel or PCM view; this packet must outlive every use of the returned pointers.
     */
    xmax_media_frame_view view() const {
        xmax_media_frame_view result{};
        result.struct_size = sizeof(result);
        result.type = type;
        result.stream_id = stream_id;
        result.is_discontinuous = discontinuous;
        result.presentation_time = presentation_time;
        result.duration = duration;
        if (type == XMAX_MEDIA_VIDEO) {
            result.video = video->view();
        } else {
            result.audio = {
                sizeof(xmax_audio_frame),
                audio.sample_rate,
                audio.channels,
                audio.samples_per_channel,
                audio.data.data(),
                audio.data.size()
            };
        }
        return result;
    }

    /**
     * Returns retained pixel or PCM bytes for output-queue admission accounting.
     */
    size_t byte_size() const {
        if (type == XMAX_MEDIA_AUDIO)
            return audio.data.size();
        size_t size = 0;
        for (const auto& plane : video->planes)
            size += plane.size();
        return size;
    }
};

/**
 * Receives an owned remote packet on the application delivery worker, outside RTC and lifecycle locks.
 */
using RemoteMediaListener = std::function<void(std::shared_ptr<const RemoteMediaFrame>)>;
} // namespace xmax::detail
