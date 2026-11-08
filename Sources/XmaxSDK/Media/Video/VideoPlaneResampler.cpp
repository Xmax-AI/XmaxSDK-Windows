//
//  VideoPlaneResampler.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/Video/VideoPlaneResampler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace xmax::detail {
namespace {

/** The source span and normalized weights contributing to one destination sample. */
struct Filter {
    /** The first source sample in the span. */
    int first;
    /** The weights in source order. */
    std::vector<float> weights;
};

/** Evaluates the normalized sinc used by the Lanczos window. */
double sinc(double value) {
    constexpr double pi = 3.14159265358979323846;
    const auto angle = pi * value;
    return angle == 0 ? 1 : std::sin(angle) / angle;
}

/** Computes pixel-centered Lanczos weights and expands the filter support when reducing an axis. */
std::vector<Filter> filters(int source, int target) {
    const double scale = double(source) / target;
    const auto footprint = std::max(1.0, scale);
    std::vector<Filter> result;
    result.reserve(target);
    for (int i = 0; i < target; ++i) {
        if (source == target) {
            result.push_back({i, {1}});
            continue;
        }
        const auto center = (i + 0.5) * scale - 0.5;
        const auto first = std::max(0, int(std::ceil(center - 3 * footprint)));
        const auto last = std::min(source - 1, int(std::floor(center + 3 * footprint)));
        Filter filter{first, {}};
        filter.weights.reserve(last - first + 1);
        double sum = 0;
        for (int sample = first; sample <= last; ++sample) {
            const auto distance = (sample - center) / footprint;
            const auto weight = sinc(distance) * sinc(distance / 3);
            filter.weights.push_back(float(weight));
            sum += weight;
        }
        for (auto& weight : filter.weights)
            weight = float(weight / sum);
        result.push_back(std::move(filter));
    }
    return result;
}

/** Reuses coefficients for full-resolution and subsampled planes on the processing thread. */
struct FilterCache {
    struct Entry {
        uint32_t source = 0, target = 0;
        std::vector<Filter> values;
    };

    std::array<Entry, 2> entries;
    size_t next = 0;

    const std::vector<Filter>& get(uint32_t source, uint32_t target) {
        for (const auto& entry : entries)
            if (entry.source == source && entry.target == target)
                return entry.values;
        auto& entry = entries[next];
        entry = {source, target, filters(source, target)};
        next = (next + 1) % entries.size();
        return entry.values;
    }
};

/** Rounds filtered samples and saturates them to unsigned bytes. */
void store_row(const float* values, size_t count, uint8_t* destination) {
    size_t index = 0;
#if defined(_M_X64) || defined(__SSE2__)
    const auto half = _mm_set1_ps(0.5f);
    const auto rounded = [half](const float* block) {
        return _mm_cvttps_epi32(_mm_add_ps(_mm_loadu_ps(block), half));
    };
    for (; index + 16 <= count; index += 16) {
        const auto low = _mm_packs_epi32(rounded(values + index), rounded(values + index + 4));
        const auto high = _mm_packs_epi32(rounded(values + index + 8), rounded(values + index + 12));
        const auto bytes = _mm_packus_epi16(low, high);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index), bytes);
    }
#endif
    for (; index < count; ++index)
        destination[index] = uint8_t(std::clamp(values[index] + 0.5f, 0.0f, 255.0f));
}

/** Resamples an interleaved plane with a fixed channel count for the inner pixel loop. */
template <size_t Channels>
void resample(
    const uint8_t* source,
    size_t stride,
    uint32_t source_width,
    uint32_t source_height,
    uint8_t* destination,
    uint32_t width,
    uint32_t height
) {
    const auto row_bytes = width * Channels;
    if (source_width == width && source_height == height) {
        for (uint32_t row = 0; row < height; ++row)
            std::memcpy(destination + row * row_bytes, source + row * stride, row_bytes);
        return;
    }

    thread_local FilterCache horizontal_cache;
    thread_local FilterCache vertical_cache;
    thread_local std::vector<float> rows;
    thread_local std::vector<float> output;

    const auto& horizontal = horizontal_cache.get(source_width, width);
    const auto& vertical = vertical_cache.get(source_height, height);
    const auto widest = std::max_element(vertical.begin(), vertical.end(), [](const auto& a, const auto& b) {
        return a.weights.size() < b.weights.size();
    });
    const auto row_count = widest->weights.size();
    rows.resize(row_count * row_bytes);
    output.resize(row_bytes);

    // Filter spans advance monotonically. Reuse a row after it leaves the current span.
    size_t cached_end = 0;
    for (const auto& filter : vertical) {
        const auto first = size_t(filter.first);
        const auto end = first + filter.weights.size();
        for (size_t i = std::max(first, cached_end); i < end; ++i) {
            const auto* input = source + i * stride;
            auto* row = rows.data() + (i % row_count) * row_bytes;
            for (size_t x = 0; x < horizontal.size(); ++x) {
                const auto& samples = horizontal[x];
                const auto* pixel = input + samples.first * Channels;
                std::array<float, Channels> color{};
                for (auto weight : samples.weights) {
                    for (size_t channel = 0; channel < Channels; ++channel)
                        color[channel] += *pixel++ * weight;
                }
                std::copy(color.begin(), color.end(), row + x * Channels);
            }
        }
        cached_end = end;
        std::fill(output.begin(), output.end(), 0.0f);
        for (size_t tap = 0; tap < filter.weights.size(); ++tap) {
            const auto weight = filter.weights[tap];
            const auto* row = rows.data() + ((first + tap) % row_count) * row_bytes;
            for (size_t x = 0; x < row_bytes; ++x)
                output[x] += row[x] * weight;
        }
        store_row(output.data(), row_bytes, destination);
        destination += row_bytes;
    }
}

} // namespace

void resample_video_plane(
    const uint8_t* source,
    size_t stride,
    uint32_t source_width,
    uint32_t source_height,
    uint8_t* destination,
    uint32_t width,
    uint32_t height,
    size_t channels
) {
    switch (channels) {
    case 4:
        resample<4>(source, stride, source_width, source_height, destination, width, height);
        break;
    case 2:
        resample<2>(source, stride, source_width, source_height, destination, width, height);
        break;
    default:
        resample<1>(source, stride, source_width, source_height, destination, width, height);
        break;
    }
}

} // namespace xmax::detail
