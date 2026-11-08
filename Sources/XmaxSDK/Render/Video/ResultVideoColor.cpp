//
//  ResultVideoColor.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Video/ResultVideoColor.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace xmax::detail {
namespace {

using Matrix = std::array<std::array<double, 3>, 3>;

// RGB transforms derived from the D65 primaries in ITU-T H.273 Table 2.
constexpr Matrix identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
constexpr Matrix pal{{{1.04404, -.04404, 0}, {0, 1, 0}, {0, .01179, .98821}}};
constexpr Matrix ntsc{{{.93954, .05018, .01028}, {.01777, .96579, .01644}, {-.00162, -.00437, 1.00599}}};
constexpr Matrix bt2020{{{1.66049, -.58764, -.07285}, {-.12455, 1.13290, -.00835}, {-.01815, -.10058, 1.11873}}};
constexpr Matrix display_p3{{{1.22494, -.22494, 0}, {-.04206, 1.04206, 0}, {-.01964, -.07864, 1.09828}}};

const Matrix* to_bt709(uint8_t primaries) {
    switch (primaries) {
    case 1: // BT.709
    case 2: // Unspecified; the RTC BGRA default is BT.709.
        return &identity;
    case 5: return &pal;
    case 6: return &ntsc;
    case 9: return &bt2020;
    case 12: return &display_p3;
    default: return nullptr;
    }
}

double bt709_to_linear(double encoded) {
    return encoded < .081 ? encoded / 4.5 : std::pow((encoded + .099) / 1.099, 1 / .45);
}

using Transfer = double (*)(double);
Transfer linear_transfer(uint8_t transfer) {
    switch (transfer) {
    case 1: case 2: case 6: case 14: case 15:
        return bt709_to_linear;
    case 4: return [](double x) { return std::pow(x, 2.2); };
    case 5: return [](double x) { return std::pow(x, 2.8); };
    case 8: return [](double x) { return x; };
    case 13: return [](double x) { return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4); };
    default: return nullptr;
    }
}

const std::array<uint8_t, 4097>& bt709_lut() {
    static const auto values = [] {
        std::array<uint8_t, 4097> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            const double x = double(i) / 4096;
            const double encoded = x < .018 ? x * 4.5 : 1.099 * std::pow(x, .45) - .099;
            result[i] = uint8_t(std::lround(encoded * 255));
        }
        return result;
    }();
    return values;
}

} // namespace

bool prepare_result_video_color(Frame& frame) {
    const auto matrix = to_bt709(frame.color_primaries);
    const auto linearize = linear_transfer(frame.color_transfer);
    if (!matrix || !linearize)
        return false;
    if (matrix == &identity && linearize == bt709_to_linear)
        return true;
    if (frame.format != XMAX_BGRA)
        return false;

    std::array<double, 256> linear{};
    for (size_t i = 0; i < linear.size(); ++i)
        linear[i] = linearize(double(i) / 255);
    const auto& encoded = bt709_lut();
    for (uint32_t y = 0; y < frame.height; ++y) {
        auto* pixel = frame.planes[0].data() + y * frame.strides[0];
        for (uint32_t x = 0; x < frame.width; ++x, pixel += 4) {
            const auto r = linear[pixel[2]], g = linear[pixel[1]], b = linear[pixel[0]];
            for (size_t channel = 0; channel < 3; ++channel) {
                const auto& row = (*matrix)[channel];
                const auto value = std::clamp(row[0] * r + row[1] * g + row[2] * b, 0.0, 1.0);
                pixel[2 - channel] = encoded[size_t(value * 4096 + .5)];
            }
        }
    }
    frame.color_primaries = frame.color_transfer = 1;
    return true;
}

} // namespace xmax::detail
