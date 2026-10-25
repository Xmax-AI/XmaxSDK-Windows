//
//  ExternalAudioConverter.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/ExternalAudioPacket.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace xmax::detail {

/**
 * Converts one serialized external PCM binding to 10 ms RTC packets using a bounded sinc resampler.
 * Format stays fixed across connections; gaps reset continuity without generating silence.
 */
class ExternalAudioConverter {

  public:

    /**
     * The synchronous packet receiver; borrowed packet storage is valid only during the call.
     */
    using Receiver = std::function<void(const ExternalAudioPacket&)>;

    /**
     * Drops partial audio and resampling history while retaining the binding's established PCM format.
     */
    void reset() {
        anchor_ = -1;
        next_time_ = -1;
        input_count_ = output_count_ = base_ = 0;
        input_.clear();
        packet_ = {};
    }

    /**
     * Copies PCM, preserves its timeline, and emits available 10 ms packets synchronously.
     *
     * @param[in] frame The source's contiguous PCM samples; storage remains valid until return.
     * @param[in] receive The synchronous receiver for owned 48 kHz mono packets.
     *
     * @throws Error If PCM layout, values, or the fixed binding format are invalid. Negative timestamps
     * and overlapping buffers are ignored. A gap discards partial output and starts a new segment.
     */
    void append(
        const xmax_external_audio_frame& frame,
        const Receiver& receive
    ) {
        require(
            frame.struct_size >= sizeof(frame),
            XMAX_MEDIA_ERROR,
            "External PCM frame structure is too small"
        );
        if (frame.timestamp_us < 0)
            return;
        require(
            frame.data &&
                frame.sample_rate >= 8000 && frame.sample_rate <= 192000 &&
                frame.channels >= 1 && frame.channels <= 8 && frame.samples_per_channel >= 1 &&
                frame.samples_per_channel <= frame.sample_rate &&
                (frame.sample_format == XMAX_AUDIO_PCM16 || frame.sample_format == XMAX_AUDIO_FLOAT32),
            XMAX_MEDIA_ERROR,
            "External audio requires interleaved PCM16 or float32 with a valid format"
        );
        const auto bytes = frame.sample_format == XMAX_AUDIO_PCM16 ? 2u : 4u;
        require(
            frame.data_size == size_t(frame.samples_per_channel) * frame.channels * bytes,
            XMAX_MEDIA_ERROR,
            "External PCM byte count does not match its format"
        );
        require(
            rate_ == 0 || (rate_ == frame.sample_rate && channels_ == frame.channels &&
                           format_ == frame.sample_format),
            XMAX_MEDIA_ERROR,
            "External PCM format cannot change during a binding"
        );
        const auto duration = double(frame.samples_per_channel) * 1000000 / frame.sample_rate;
        require(
            frame.timestamp_us <= std::numeric_limits<int64_t>::max() - int64_t(std::ceil(duration)),
            XMAX_MEDIA_ERROR,
            "External PCM timestamp exceeds the supported range"
        );
        const double difference = next_time_ < 0 ? 0 : double(frame.timestamp_us - next_time_) - last_duration_;
        const double tolerance = 500000.0 / frame.sample_rate;
        if (difference < -tolerance)
            return;
        if (difference > tolerance)
            reset();
        if (anchor_ < 0)
            anchor_ = frame.timestamp_us;
        rate_ = frame.sample_rate;
        channels_ = frame.channels;
        format_ = frame.sample_format;

        const auto* data = static_cast<const uint8_t*>(frame.data);
        std::vector<double> mono(frame.samples_per_channel);
        for (uint32_t sample = 0; sample < frame.samples_per_channel; ++sample) {
            double sum = 0;
            for (uint32_t channel = 0; channel < frame.channels; ++channel) {
                const auto offset = (size_t(sample) * frame.channels + channel) * bytes;
                if (frame.sample_format == XMAX_AUDIO_PCM16) {
                    int16_t value;
                    std::memcpy(
                        &value,
                        data + offset,
                        sizeof(value)
                    );
                    sum += value / 32768.0;
                } else {
                    float value;
                    std::memcpy(
                        &value,
                        data + offset,
                        sizeof(value)
                    );
                    require(
                        std::isfinite(value) && value >= -1 && value <= 1,
                        XMAX_MEDIA_ERROR,
                        "External float PCM samples must be finite and in [-1, 1]"
                    );
                    sum += value;
                }
            }
            mono[sample] = sum / frame.channels;
        }
        input_.insert(
            input_.end(),
            mono.begin(),
            mono.end()
        );
        input_count_ += frame.samples_per_channel;
        next_time_ = frame.timestamp_us;
        last_duration_ = duration;
        const double cutoff = std::min(1.0, 48000.0 / rate_);
        const int radius = int(std::ceil(16 / cutoff));

        while (true) {
            const double position = double(output_count_) * rate_ / 48000;
            if (rate_ == 48000 ? position >= input_count_ : position + radius >= input_count_)
                break;
            double value = 0;
            if (rate_ == 48000) {
                value = input_[size_t(output_count_ - base_)];
            } else {
                double weight = 0;
                const auto center = int64_t(std::floor(position));
                for (int64_t index = center - radius + 1; index <= center + radius; ++index) {
                    const double distance = position - index;
                    if (std::abs(distance) >= radius)
                        continue;
                    const double phase = 3.14159265358979323846 * distance * cutoff;
                    const double sinc = std::abs(phase) < 1e-12 ? 1 : std::sin(phase) / phase;
                    const double window = 0.5 + 0.5 * std::cos(3.14159265358979323846 * distance / radius);
                    const double coefficient = cutoff * sinc * window;
                    const auto sample = std::max<int64_t>(0, index);
                    value += input_[size_t(sample - base_)] * coefficient;
                    weight += coefficient;
                }
                value /= weight;
            }
            if (packet_.samples.empty()) {
                const auto offset = output_count_ / 48000 * 1000000 + output_count_ % 48000 * 1000000 / 48000;
                require(
                    offset <= std::numeric_limits<int64_t>::max() - anchor_,
                    XMAX_MEDIA_ERROR,
                    "Converted PCM timestamp exceeds the supported range"
                );
                packet_.timestamp_us = anchor_ + offset;
            }
            packet_.samples.push_back(int16_t(std::clamp(std::lround(value * 32768), -32768l, 32767l)));
            ++output_count_;
            if (packet_.samples.size() == 480) {
                receive(packet_);
                packet_.samples.clear();
            }
        }
        const auto keep = std::max<int64_t>(0, int64_t(double(output_count_) * rate_ / 48000) - radius - 1);
        if (keep > base_) {
            input_.erase(
                input_.begin(),
                input_.begin() + (keep - base_)
            );
            base_ = keep;
        }
    }

  private:

    /**
     * The binding's sample rate, or zero until its first published audio buffer.
     */
    uint32_t rate_ = 0;

    /**
     * The fixed interleaved source channel count.
     */
    uint32_t channels_ = 0;

    /**
     * The fixed source sample representation.
     */
    xmax_audio_sample_format format_ = XMAX_AUDIO_PCM16;

    /**
     * The current segment's presentation anchor in microseconds; -1 before input.
     */
    int64_t anchor_ = -1;

    /**
     * The previous buffer's exact first-sample timestamp in microseconds; -1 before input.
     */
    int64_t next_time_ = -1;

    /**
     * The previous source buffer duration in microseconds, retaining fractional samples.
     */
    double last_duration_ = 0;

    /**
     * The input sample count in this contiguous segment.
     */
    int64_t input_count_ = 0;

    /**
     * The output sample count in this contiguous segment.
     */
    int64_t output_count_ = 0;

    /**
     * The segment sample index of the first retained resampler sample.
     */
    int64_t base_ = 0;

    /**
     * The bounded source samples needed for the current call and subsequent filter history.
     */
    std::vector<double> input_;

    /**
     * The incomplete output packet, always smaller than 10 ms between submissions.
     */
    ExternalAudioPacket packet_;
};
} // namespace xmax::detail
