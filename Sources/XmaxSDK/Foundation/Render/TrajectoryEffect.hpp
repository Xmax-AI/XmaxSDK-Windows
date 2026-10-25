//
//  TrajectoryEffect.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <algorithm>
#include <cmath>

namespace xmax::detail {

/**
 * One fading stroke in normalized displayed-video coordinates.
 */
struct TrajectoryStroke {

    /**
     * The ordered normalized coordinates for this stroke.
     */
    std::vector<xmax_point> points;

    /**
     * The monotonic seconds since the stroke was sampled.
     */
    double age = 0;
};

/**
 * Generates premultiplied BGRA glow, fading trails, pulsing rings, and orbiting pointer particles.
 * Coordinates supplied to drawing operations are local bitmap pixels.
 */
class TrajectoryBitmap {

  public:

    /**
     * Allocates a transparent effect bitmap within the specified viewport.
     *
     * @param[in] width The bitmap width in pixels, greater than zero.
     * @param[in] height The bitmap height in pixels, greater than zero.
     */
    TrajectoryBitmap(
        int width,
        int height
    ) : width_(width), height_(height), pixels_(size_t(width) * height, 0) {
    }

    /**
     * Returns immutable top-down, premultiplied BGRA pixels for alpha compositing.
     */
    const std::vector<uint32_t>& pixels() const {
        return pixels_;
    }

    /**
     * Paints an antialiased glowing trail segment with a white center.
     *
     * @param[in] from The first endpoint in local bitmap pixels.
     * @param[in] to The last endpoint in local bitmap pixels.
     * @param[in] opacity The fading stroke opacity in [0, 1].
     * @param[in] scale The display scale relative to a 96-DPI viewport.
     */
    void segment(
        xmax_point from,
        xmax_point to,
        double opacity,
        double scale
    ) {
        const double radius = 18 * scale;
        const double dx = to.x - from.x, dy = to.y - from.y;
        const double length = dx * dx + dy * dy;
        const int left = std::max(0, int(std::floor(std::min(from.x, to.x) - radius)));
        const int right = std::min(width_ - 1, int(std::ceil(std::max(from.x, to.x) + radius)));
        const int top = std::max(0, int(std::floor(std::min(from.y, to.y) - radius)));
        const int bottom = std::min(height_ - 1, int(std::ceil(std::max(from.y, to.y) + radius)));
        for (int y = top; y <= bottom; ++y) {
            int row_left = left, row_right = right;
            if (std::abs(dy) > 1e-9) {
                const double a = (y - radius - from.y) / dy, b = (y + radius - from.y) / dy;
                const double first = std::clamp(std::min(a, b), 0.0, 1.0);
                const double last = std::clamp(std::max(a, b), 0.0, 1.0);
                const double x1 = from.x + first * dx, x2 = from.x + last * dx;
                row_left = std::max(left, int(std::floor(std::min(x1, x2) - radius)));
                row_right = std::min(right, int(std::ceil(std::max(x1, x2) + radius)));
            }
            for (int x = row_left; x <= row_right; ++x) {
                const double t = length == 0 ? 0 : std::clamp(((x - from.x) * dx + (y - from.y) * dy) / length, 0.0, 1.0);
                const double distance = std::hypot(
                                            x - from.x - t * dx,
                                            y - from.y - t * dy
                                        ) /
                                        scale;
                const double glow = .22 * std::exp(-distance * distance / 72) + .52 * std::exp(-distance * distance / 18);
                blend(
                    x,
                    y,
                    glow * opacity,
                    false
                );
                blend(
                    x,
                    y,
                    .82 * opacity * std::clamp(2 - distance, 0.0, 1.0),
                    true
                );
            }
        }
    }

    /**
     * Paints animated pointer rings, particles, and the radial head glow.
     *
     * @param[in] center The pointer location in local bitmap pixels.
     * @param[in] elapsed The monotonic gesture age in seconds.
     * @param[in] opacity The current pointer opacity in [0, 1].
     * @param[in] scale The display scale relative to a 96-DPI viewport.
     */
    void pointer(
        xmax_point center,
        double elapsed,
        double opacity,
        double scale
    ) {
        const double pi = 3.14159265358979323846;
        for (int ring = 0; ring < 2; ++ring) {
            const double pulse = (std::sin((elapsed * 1.2 + ring * .5) * pi * 2) + 1) / 2;
            const double radius = (14 + ring * 18 + pulse * 8) * scale;
            const double alpha = .5 * (1 - ring * .2) * (.5 + pulse * .5) * opacity;
            disc(
                center,
                radius,
                alpha,
                false,
                scale
            );
        }
        for (int index = 0; index < 4; ++index) {
            const double angle = index / 4.0 * pi * 2 + elapsed * .06 * (index % 2 ? -1 : 1);
            const xmax_point point{center.x + std::cos(angle) * 22 * scale, center.y + std::sin(angle) * 22 * scale};
            const double alpha = .6 * (.6 + std::sin(elapsed * 3 + index) * .4) * opacity;
            disc(
                point,
                5 * scale,
                alpha,
                true,
                scale
            );
        }
        disc(
            center,
            16 * scale,
            .9 * opacity,
            true,
            scale
        );
        disc(
            center,
            5 * scale,
            opacity,
            true,
            scale,
            true
        );
    }

  private:

    /**
     * The effect bitmap width in pixels.
     */
    int width_;

    /**
     * The effect bitmap height in pixels.
     */
    int height_;

    /**
     * The top-down premultiplied BGRA effect pixels.
     */
    std::vector<uint32_t> pixels_;

    /**
     * Composites one bounded green-glow or white-core sample into the bitmap.
     *
     * @param[in] x The pixel column inside the bitmap.
     * @param[in] y The pixel row inside the bitmap.
     * @param[in] alpha The sample opacity.
     * @param[in] white Whether to use the white center instead of the green glow.
     */
    void blend(
        int x,
        int y,
        double alpha,
        bool white
    ) {
        const auto a = uint32_t(std::clamp(alpha, 0.0, 1.0) * 255);
        auto& pixel = pixels_[size_t(y) * width_ + x];
        const auto inverse = 255 - a;
        const auto b = (white ? a : a * 100 / 255) + (pixel & 255) * inverse / 255;
        const auto g = a + ((pixel >> 8) & 255) * inverse / 255;
        const auto r = (white ? a : 0) + ((pixel >> 16) & 255) * inverse / 255;
        const auto out_a = a + (pixel >> 24) * inverse / 255;
        pixel = b | (g << 8) | (r << 16) | (out_a << 24);
    }

    /**
     * Paints a radial glow, antialiased ring, or solid pointer center.
     *
     * @param[in] center The center in local bitmap pixels.
     * @param[in] radius The radius in pixels.
     * @param[in] opacity The effect opacity.
     * @param[in] filled Whether to fill the disc rather than draw a ring.
     * @param[in] scale The display scale.
     * @param[in] white Whether this is the solid white pointer center.
     */
    void disc(
        xmax_point center,
        double radius,
        double opacity,
        bool filled,
        double scale,
        bool white = false
    ) {
        const int left = std::max(0, int(std::floor(center.x - radius - scale)));
        const int right = std::min(width_ - 1, int(std::ceil(center.x + radius + scale)));
        const int top = std::max(0, int(std::floor(center.y - radius - scale)));
        const int bottom = std::min(height_ - 1, int(std::ceil(center.y + radius + scale)));
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x) {
                const double distance = std::hypot(
                    x - center.x,
                    y - center.y
                );
                const double coverage = filled
                                            ? (white ? std::clamp((radius - distance) / scale, 0.0, 1.0)
                                                     : std::pow(
                                                           std::max(0.0, 1 - distance / radius),
                                                           2
                                                       ))
                                            : std::clamp(1.5 - std::abs(distance - radius) / scale, 0.0, 1.0);
                blend(
                    x,
                    y,
                    opacity * coverage,
                    white
                );
            }
    }
};
} // namespace xmax::detail
