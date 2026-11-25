//
//  ResultYuvColor.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <AgoraMediaBase.h>
#include <algorithm>
#include <cmath>

namespace xmax::beauty_worker {

// The SDK's prepared BGRA is full-range BT.709. ClearVision accepts tightly packed,
// video-range I420. Use one chroma sample for the four pixels in each 2x2 block.
inline void write_bt709_i420(const uint8_t* bgra, uint32_t width, uint32_t height, uint8_t* output) {
    const size_t area = size_t(width) * height;
    auto* u = output + area;
    auto* v = u + area / 4;
    for (uint32_t y = 0; y < height; y += 2) {
        for (uint32_t x = 0; x < width; x += 2) {
            int red = 0, green = 0, blue = 0;
            for (uint32_t row = 0; row < 2; ++row) {
                const size_t index = size_t(y + row) * width + x;
                for (uint32_t col = 0; col < 2; ++col) {
                    const auto* pixel = bgra + 4 * (index + col);
                    const int b = pixel[0], g = pixel[1], r = pixel[2];
                    // BT.709 coefficients scaled by 65536, including the 219/255 luma range.
                    output[index + col] = uint8_t(16 + ((11966 * r + 40254 * g + 4064 * b + 32768) >> 16));
                    red += r;
                    green += g;
                    blue += b;
                }
            }
            // Round once, after averaging the block and scaling chroma to the 224-code range.
            constexpr int center = (128 << 18) + (1 << 17);
            *u++ = uint8_t((center - 6596 * red - 22188 * green + 28784 * blue) >> 18);
            *v++ = uint8_t((center + 28784 * red - 26145 * green - 2639 * blue) >> 18);
        }
    }
}

// Reconstruct full-range BGRA from the filter's I420 output using its stated matrix and range.
class ResultYuvColor {
  public:
    explicit ResultYuvColor(const agora::media::base::ColorSpace& color) {
        using Color = agora::media::base::ColorSpace;
        double kr = .2126, kb = .0722;
        if (color.matrix == Color::MATRIXID_BT470BG || color.matrix == Color::MATRIXID_SMPTE170M) {
            kr = .299;
            kb = .114;
        } else if (color.matrix == Color::MATRIXID_BT2020_NCL) {
            kr = .2627;
            kb = .0593;
        }
        const bool full_range = color.range == Color::RANGEID_FULL;
        const double chroma = full_range ? 256.0 : 256.0 * 255 / 224;
        const double kg = 1 - kr - kb;
        y_offset_ = full_range ? 0 : 16;
        y_scale_ = full_range ? 256 : int(std::lround(256.0 * 255 / 219));
        blue_u_ = int(std::lround(2 * (1 - kb) * chroma));
        green_u_ = int(std::lround(2 * kb * (1 - kb) / kg * chroma));
        green_v_ = int(std::lround(2 * kr * (1 - kr) / kg * chroma));
        red_v_ = int(std::lround(2 * (1 - kr) * chroma));
    }

    void write_bgra(uint8_t y, uint8_t u, uint8_t v, uint8_t* output) const noexcept {
        const auto luma = y_scale_ * (int(y) - y_offset_);
        const int cb = int(u) - 128, cr = int(v) - 128;
        const auto byte = [](int value) { return uint8_t(std::clamp((value + 128) / 256, 0, 255)); };
        output[0] = byte(luma + blue_u_ * cb);
        output[1] = byte(luma - green_u_ * cb - green_v_ * cr);
        output[2] = byte(luma + red_v_ * cr);
        output[3] = 255;
    }

  private:
    int y_offset_, y_scale_, blue_u_, green_u_, green_v_, red_v_;
};

} // namespace xmax::beauty_worker
