//
//  ExternalVideoResampler.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/External/ExternalVideoResampler.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

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

    const auto horizontal = filters(source_width, width);
    const auto vertical = filters(source_height, height);
    std::deque<std::vector<float>> rows;
    std::vector<float> output(row_bytes);
    int first_row = 0;
    for (const auto& filter : vertical) {
        while (!rows.empty() && first_row < filter.first) {
            rows.pop_front();
            ++first_row;
        }
        if (rows.empty())
            first_row = filter.first;
        while (rows.size() < filter.weights.size()) {
            const auto* input = source + (first_row + rows.size()) * stride;
            auto& row = rows.emplace_back(row_bytes);
            for (size_t x = 0; x < horizontal.size(); ++x) {
                const auto& samples = horizontal[x];
                const auto* pixel = input + samples.first * Channels;
                for (auto weight : samples.weights) {
                    for (size_t channel = 0; channel < Channels; ++channel)
                        row[x * Channels + channel] += *pixel++ * weight;
                }
            }
        }
        std::fill(output.begin(), output.end(), 0);
        for (size_t tap = 0; tap < filter.weights.size(); ++tap) {
            const auto weight = filter.weights[tap];
            const auto& row = rows[tap];
            for (size_t x = 0; x < row_bytes; ++x)
                output[x] += row[x] * weight;
        }
        for (auto value : output)
            *destination++ = uint8_t(std::clamp(value + 0.5f, 0.0f, 255.0f));
    }
}

} // namespace

void resample_external_plane(
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
