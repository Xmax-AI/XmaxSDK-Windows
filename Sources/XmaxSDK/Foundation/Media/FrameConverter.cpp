//
//  FrameConverter.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/FrameConverter.hpp"
#include <algorithm>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace xmax::detail {
namespace {

template <bool Nv12>
void convert_yuv_rows(const uint8_t* y0, const uint8_t* y1, const uint8_t* u, const uint8_t* v,
                      uint8_t* output0, uint8_t* output1, uint32_t width) {
    uint32_t x = 0;
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128();
    const auto center = _mm_set1_epi16(128);
    const auto rounding = _mm_set1_epi32(128);
    const auto luma_coefficient = _mm_set1_epi32(298);
    const auto blue_coefficient = _mm_set1_epi32(516);
    const auto red_coefficient = _mm_set1_epi32(409);
    const auto green_coefficients = _mm_set_epi16(-208, -100, -208, -100, -208, -100, -208, -100);
    for (; x + 8 <= width; x += 8) {
        __m128i us, vs;
        if constexpr (Nv12) {
            const auto uv = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(u + x));
            us = _mm_and_si128(uv, _mm_set1_epi16(255));
            vs = _mm_srli_epi16(uv, 8);
        } else {
            uint32_t packed_u, packed_v;
            std::memcpy(&packed_u, u + x / 2, sizeof(packed_u));
            std::memcpy(&packed_v, v + x / 2, sizeof(packed_v));
            us = _mm_unpacklo_epi8(_mm_cvtsi32_si128(int(packed_u)), zero);
            vs = _mm_unpacklo_epi8(_mm_cvtsi32_si128(int(packed_v)), zero);
        }
        us = _mm_sub_epi16(_mm_unpacklo_epi16(us, us), center);
        vs = _mm_sub_epi16(_mm_unpacklo_epi16(vs, vs), center);
        const auto blue0 = _mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(us, zero), blue_coefficient), rounding);
        const auto blue1 = _mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(us, zero), blue_coefficient), rounding);
        const auto red0 = _mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(vs, zero), red_coefficient), rounding);
        const auto red1 = _mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(vs, zero), red_coefficient), rounding);
        const auto green0 = _mm_add_epi32(_mm_madd_epi16(_mm_unpacklo_epi16(us, vs), green_coefficients), rounding);
        const auto green1 = _mm_add_epi32(_mm_madd_epi16(_mm_unpackhi_epi16(us, vs), green_coefficients), rounding);
        const auto convert = [&](const uint8_t* input, uint8_t* output) {
            const auto ys = _mm_sub_epi16(_mm_unpacklo_epi8(
                _mm_loadl_epi64(reinterpret_cast<const __m128i*>(input + x)), zero), _mm_set1_epi16(16));
            const auto luma0 = _mm_madd_epi16(_mm_unpacklo_epi16(ys, zero), luma_coefficient);
            const auto luma1 = _mm_madd_epi16(_mm_unpackhi_epi16(ys, zero), luma_coefficient);
            const auto channel = [&](const __m128i low, const __m128i high) {
                const auto values = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(luma0, low), 8),
                                                   _mm_srai_epi32(_mm_add_epi32(luma1, high), 8));
                return _mm_packus_epi16(values, zero);
            };
            const auto bg = _mm_unpacklo_epi8(channel(blue0, blue1), channel(green0, green1));
            const auto ra = _mm_unpacklo_epi8(channel(red0, red1), _mm_set1_epi8(-1));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(output + x * 4), _mm_unpacklo_epi16(bg, ra));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(output + x * 4 + 16), _mm_unpackhi_epi16(bg, ra));
        };
        convert(y0, output0);
        convert(y1, output1);
    }
#endif
    const auto byte = [](int value) { return uint8_t(std::clamp(value, 0, 255)); };
    for (; x < width; x += 2) {
        const int cb = int(u[Nv12 ? x : x / 2]) - 128;
        const int cr = int(Nv12 ? u[x + 1] : v[x / 2]) - 128;
        const int blue = 516 * cb + 128, green = -100 * cb - 208 * cr + 128, red = 409 * cr + 128;
        const auto convert = [&](const uint8_t* input, uint8_t* output) {
            for (uint32_t column = x; column < x + 2; ++column) {
                const int luma = 298 * (int(input[column]) - 16);
                auto* pixel = output + column * 4;
                pixel[0] = byte((luma + blue) >> 8);
                pixel[1] = byte((luma + green) >> 8);
                pixel[2] = byte((luma + red) >> 8);
                pixel[3] = 255;
            }
        };
        convert(y0, output0);
        convert(y1, output1);
    }
}

} // namespace

/**
 * Converts or copies a supported frame to owned BGRA pixels.
 *
 * @param[in] source The valid source frame; its pixels must remain valid until this synchronous call
 * returns.
 *
 * @return An independently owned BGRA frame preserving source dimensions and timestamp.
 */
Frame bgra_frame(
    const Frame& source
) {
    if (source.format == XMAX_BGRA) {
        auto result = Frame::copy(source.view());
        result.color_primaries = source.color_primaries;
        result.color_transfer = source.color_transfer;
        result.presentation_mirrored = source.presentation_mirrored;
        return result;
    }

    validate_frame(source.view());

    Frame result;
    result.width = source.width;
    result.height = source.height;
    result.timestamp_us = source.timestamp_us;
    result.presentation_mirrored = source.presentation_mirrored;
    result.color_primaries = source.color_primaries;
    result.color_transfer = source.color_transfer;
    result.strides[0] = size_t(result.width) * 4;
    result.planes[0].resize(result.strides[0] * result.height);

    for (uint32_t y = 0; y < source.height; y += 2) {
        const auto* y0 = source.planes[0].data() + y * source.strides[0];
        const auto* y1 = y0 + source.strides[0];
        const auto* u = source.planes[1].data() + y / 2 * source.strides[1];
        auto* output0 = result.planes[0].data() + y * result.strides[0];
        auto* output1 = output0 + result.strides[0];
        if (source.format == XMAX_NV12)
            convert_yuv_rows<true>(y0, y1, u, nullptr, output0, output1, source.width);
        else
            convert_yuv_rows<false>(y0, y1, u, source.planes[2].data() + y / 2 * source.strides[2],
                                     output0, output1, source.width);
    }

    return result;
}

Frame rotate_camera_frame(Frame frame, unsigned degrees) {
    if (degrees != 90 && degrees != 180 && degrees != 270)
        return frame;
    Frame result;
    result.format = frame.format;
    result.width = degrees == 180 ? frame.width : frame.height;
    result.height = degrees == 180 ? frame.height : frame.width;
    result.timestamp_us = frame.timestamp_us;
    result.presentation_mirrored = frame.presentation_mirrored;
    result.color_primaries = frame.color_primaries;
    result.color_transfer = frame.color_transfer;
    const auto rotate_plane = [&](size_t plane, uint32_t divisor, size_t pixel_bytes) {
        const auto width = frame.width / divisor;
        const auto height = frame.height / divisor;
        result.strides[plane] = (result.width / divisor) * pixel_bytes;
        result.planes[plane].resize(result.strides[plane] * (result.height / divisor));
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const auto column = degrees == 270 ? y : degrees == 180 ? width - 1 - x : height - 1 - y;
                const auto row = degrees == 90 ? x : degrees == 180 ? height - 1 - y : width - 1 - x;
                std::memcpy(result.planes[plane].data() + row * result.strides[plane] + column * pixel_bytes,
                            frame.planes[plane].data() + y * frame.strides[plane] + x * pixel_bytes, pixel_bytes);
            }
        }
    };
    rotate_plane(0, 1, frame.format == XMAX_NV12 ? 1 : 4);
    if (frame.format == XMAX_NV12)
        rotate_plane(1, 2, 2);
    return result;
}

} // namespace xmax::detail
