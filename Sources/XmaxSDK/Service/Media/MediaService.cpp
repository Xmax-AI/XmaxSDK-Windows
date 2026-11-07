//
//  MediaService.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Media/MediaService.hpp"
#include <algorithm>
#include <numeric>

namespace xmax::detail {

xmax_video_format MediaService::external_input(
    xmax_video_format requested,
    const xmax_model_info& model
) {
    validate_format(requested);

    constexpr uint32_t alignment = 4;
    constexpr uint32_t maximum_dimension_units = 8192 / alignment;
    constexpr uint32_t unit_area = alignment * alignment;
    const auto minimum_units = (model.minimum_input_pixels + unit_area - 1) / unit_area;
    const auto maximum_units = model.maximum_input_pixels / unit_area;
    const double area = double(requested.width) * requested.height;
    const double target = std::clamp(
        area,
        double(model.minimum_input_pixels),
        double(model.maximum_input_pixels)
    );
    const double scale = std::sqrt(target / area);

    const auto divisor = std::gcd(requested.width, requested.height);
    const auto ratio_width = requested.width / divisor;
    const auto ratio_height = requested.height / divisor;
    const auto ratio_area = double(ratio_width) * ratio_height;
    const auto exact_units = uint32_t(std::max(
        std::floor(divisor * scale / alignment),
        std::ceil(std::sqrt(model.minimum_input_pixels / ratio_area) / alignment)
    ));
    const auto fits = [&](uint32_t width, uint32_t height) {
        return width && height && width <= maximum_dimension_units && height <= maximum_dimension_units &&
               width * height >= minimum_units && width * height <= maximum_units;
    };
    if (fits(ratio_width * exact_units, ratio_height * exact_units))
        return {ratio_width * exact_units * alignment, ratio_height * exact_units * alignment, requested.fps};

    const double width = requested.width * scale / alignment;
    const double height = requested.height * scale / alignment;
    if (fits(uint32_t(width), uint32_t(height)))
        return {uint32_t(width) * alignment, uint32_t(height) * alignment, requested.fps};

    xmax_video_format result{};
    double best_distance = std::numeric_limits<double>::infinity();
    for (uint32_t w = 1; w <= std::min(maximum_units, maximum_dimension_units); ++w) {
        const auto minimum_height = std::max(1u, (minimum_units + w - 1) / w);
        const auto maximum_height = std::min(maximum_units / w, maximum_dimension_units);
        if (minimum_height > maximum_height)
            continue;
        const auto h = std::clamp(uint32_t(std::round(height)), minimum_height, maximum_height);
        const double dw = (w - width) / width;
        const double dh = (h - height) / height;
        const double distance = dw * dw + dh * dh;
        if (distance < best_distance) {
            best_distance = distance;
            result = {w * alignment, h * alignment, requested.fps};
        }
    }
    validate_format(result);
    return result;
}

xmax_video_format MediaService::target(
    xmax_video_format source,
    double maximum_width,
    double maximum_height
) {
    validate_format(source);
    require(std::isfinite(maximum_width) && std::isfinite(maximum_height) &&
                maximum_width > 0 && maximum_height > 0,
            XMAX_INVALID_ARGUMENT, "Invalid target video bounds");
    const auto scale = std::min({1.0, maximum_width / source.width, maximum_height / source.height});
    const auto gcd = std::gcd(source.width, source.height);
    const auto exact = uint32_t(std::floor(gcd * scale / 4)) * 4;
    if (exact)
        return {source.width / gcd * exact, source.height / gcd * exact, source.fps};
    const auto width = uint32_t(std::floor(source.width * scale / 4)) * 4;
    const auto height = uint32_t(std::floor(source.height * scale / 4)) * 4;
    require(width >= 4 && height >= 4, XMAX_INVALID_ARGUMENT, "Target bounds cannot fit a 4 by 4 frame");
    return {width, height, source.fps};
}

} // namespace xmax::detail
