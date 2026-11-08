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
