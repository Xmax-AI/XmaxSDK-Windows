//
//  RemoteAudioTimeline.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/AudioFrame.hpp"
#include <limits>
#include <optional>
#include <cmath>

namespace xmax::detail {

/**
 * Advances remote PCM by exact sample counts while retaining its offset from the video rendering clock.
 */
class RemoteAudioTimeline {

  public:

    /**
     * Clears sample continuity after listener replacement, invalid input, or result-stream replacement.
     */
    void reset() {
        next_.reset();
        sample_rate_ = channels_ = 0;
    }

    /**
     * Resolves exact sample time, reanchoring on format changes or native clock differences over 100 ms.
     *
     * @param[in] frame The copied PCM and native rendering timestamp; invalid input resets continuity.
     * @param[out] time Receives presentation time in units of 1 / (sample_rate * 1000) seconds.
     * @param[out] discontinuous Receives whether sample continuity was restarted.
     *
     * @return Whether the packet has valid PCM and a usable positive native rendering timestamp.
     */
    bool resolve(
        const AudioFrame& frame,
        xmax_media_time& time,
        bool& discontinuous
    ) {
        if (frame.render_time_ms <= 0 || frame.sample_rate < 8000 || frame.sample_rate > 192000 ||
            frame.channels < 1 || frame.channels > 2 || frame.samples_per_channel < 1 ||
            frame.samples_per_channel > 192000 ||
            frame.data.size() != size_t(frame.samples_per_channel) * frame.channels * 2 ||
            frame.render_time_ms >
                (std::numeric_limits<int64_t>::max() - int64_t(frame.samples_per_channel) * 1000) /
                    frame.sample_rate) {
            reset();
            return false;
        }

        const auto native = frame.render_time_ms * frame.sample_rate;
        const bool format_changed = sample_rate_ != frame.sample_rate || channels_ != frame.channels;
        discontinuous = !next_ || format_changed;
        auto value = next_.value_or(native);
        if (format_changed ||
            std::fabs(static_cast<long double>(native) - value) > frame.sample_rate * 100.0L) {
            value = native;
            discontinuous = true;
        }
        const auto duration = int64_t(frame.samples_per_channel) * 1000;
        if (value > std::numeric_limits<int64_t>::max() - duration) {
            reset();
            return false;
        }
        time = {value, int32_t(frame.sample_rate * 1000)};
        next_ = value + duration;
        sample_rate_ = frame.sample_rate;
        channels_ = frame.channels;
        return true;
    }

  private:

    /**
     * The next PCM time in units of 1 / (sample_rate * 1000) seconds; empty before a valid packet.
     */
    std::optional<int64_t> next_;

    /**
     * The sample rate defining next_'s time units.
     */
    uint32_t sample_rate_ = 0;

    /**
     * The channel count defining the current continuous format.
     */
    uint32_t channels_ = 0;
};
} // namespace xmax::detail
