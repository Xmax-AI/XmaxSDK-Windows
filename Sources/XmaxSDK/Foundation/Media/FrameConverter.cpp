#include "Foundation/Media/FrameConverter.hpp"
#include <algorithm>
#include <cmath>
namespace xmax::detail {
Frame bgra_frame(const Frame& source) {
    if (source.format == XMAX_BGRA)
        return Frame::copy(source.view());
    validate_frame(source.view());
    Frame result;
    result.width = source.width;
    result.height = source.height;
    result.timestamp_us = source.timestamp_us;
    result.strides[0] = size_t(result.width) * 4;
    result.planes[0].resize(result.strides[0] * result.height);
    const auto byte = [](int value) { return uint8_t(std::clamp(value, 0, 255)); };
    for (uint32_t y = 0; y < source.height; ++y)
        for (uint32_t x = 0; x < source.width; ++x) {
            const int luma = int(source.planes[0][y * source.strides[0] + x]) - 16;
            int u, v;
            if (source.format == XMAX_NV12) {
                const auto index = (y / 2) * source.strides[1] + (x / 2) * 2;
                u = int(source.planes[1][index]) - 128;
                v = int(source.planes[1][index + 1]) - 128;
            } else {
                u = int(source.planes[1][(y / 2) * source.strides[1] + x / 2]) - 128;
                v = int(source.planes[2][(y / 2) * source.strides[2] + x / 2]) - 128;
            }
            auto* pixel = result.planes[0].data() + y * result.strides[0] + x * 4;
            pixel[0] = byte((298 * luma + 516 * u + 128) >> 8);
            pixel[1] = byte((298 * luma - 100 * u - 208 * v + 128) >> 8);
            pixel[2] = byte((298 * luma + 409 * v + 128) >> 8);
            pixel[3] = 255;
        }
    return result;
}
Frame fit_frame(Frame source, xmax_video_format format) {
    validate_format(format);
    validate_frame(source.view());
    auto input = source.format == XMAX_BGRA ? std::move(source) : bgra_frame(source);
    if (input.width == format.width && input.height == format.height)
        return input;
    Frame output;
    output.width = format.width;
    output.height = format.height;
    output.timestamp_us = input.timestamp_us;
    output.strides[0] = size_t(output.width) * 4;
    output.planes[0].assign(output.strides[0] * output.height, 0);
    for (size_t i = 3; i < output.planes[0].size(); i += 4)
        output.planes[0][i] = 255;
    const double scale = std::min(double(output.width) / input.width, double(output.height) / input.height);
    const uint32_t width = std::max(1u, uint32_t(std::round(input.width * scale)));
    const uint32_t height = std::max(1u, uint32_t(std::round(input.height * scale)));
    const uint32_t left = (output.width - width) / 2, top = (output.height - height) / 2;
    // Bilinear sampling keeps the negotiated canvas independent from camera hardware formats.
    for (uint32_t y = 0; y < height; ++y) {
        const double sy = std::clamp((y + 0.5) / scale - 0.5, 0.0, double(input.height - 1));
        const auto y0 = uint32_t(sy), y1 = std::min(y0 + 1, input.height - 1);
        const double fy = sy - y0;
        for (uint32_t x = 0; x < width; ++x) {
            const double sx = std::clamp((x + 0.5) / scale - 0.5, 0.0, double(input.width - 1));
            const auto x0 = uint32_t(sx), x1 = std::min(x0 + 1, input.width - 1);
            const double fx = sx - x0;
            for (size_t c = 0; c < 4; ++c) {
                const auto sample = [&](uint32_t px, uint32_t py) {
                    return input.planes[0][py * input.strides[0] + px * 4 + c];
                };
                const double upper = sample(x0, y0) * (1 - fx) + sample(x1, y0) * fx;
                const double lower = sample(x0, y1) * (1 - fx) + sample(x1, y1) * fx;
                output.planes[0][(y + top) * output.strides[0] + (x + left) * 4 + c] =
                    uint8_t(std::round(upper * (1 - fy) + lower * fy));
            }
        }
    }
    return output;
}
} // namespace xmax::detail
