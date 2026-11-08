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

namespace {

bool positive_dimension(double value) {
    return std::isfinite(value) && value > 0;
}

uint64_t integral_dimension(double value) {
    require(positive_dimension(value) && value < double(std::numeric_limits<int64_t>::max()) &&
                value == std::floor(value),
            XMAX_INVALID_ARGUMENT, "Media dimensions must be positive integers");
    return uint64_t(value);
}

xmax_media_size external_size(
    xmax_media_size source,
    const xmax_model_info& model,
    uint32_t maximum_dimension_units
) {
    const auto width = integral_dimension(source.width);
    const auto height = integral_dimension(source.height);
    constexpr uint32_t alignment = 4;
    constexpr uint32_t unit_area = alignment * alignment;
    const auto minimum_units = (model.minimum_input_pixels + unit_area - 1) / unit_area;
    const auto maximum_units = model.maximum_input_pixels / unit_area;
    const double area = source.width * source.height;
    const double target = std::clamp(area, double(model.minimum_input_pixels), double(model.maximum_input_pixels));
    const double scale = std::sqrt(target / area);

    const auto divisor = std::gcd(width, height);
    const double ratio_width = double(width / divisor);
    const double ratio_height = double(height / divisor);
    const auto exact_units = std::max(
        std::floor(double(divisor) * scale / alignment),
        std::ceil(std::sqrt(model.minimum_input_pixels / (ratio_width * ratio_height)) / alignment)
    );
    const auto fits = [&](double w, double h) {
        return w >= 1 && h >= 1 && w <= maximum_dimension_units && h <= maximum_dimension_units &&
               w * h >= minimum_units && w * h <= maximum_units;
    };
    if (fits(ratio_width * exact_units, ratio_height * exact_units))
        return {ratio_width * exact_units * alignment, ratio_height * exact_units * alignment};

    const double w = source.width * scale / alignment;
    const double h = source.height * scale / alignment;
    if (fits(std::floor(w), std::floor(h)))
        return {std::floor(w) * alignment, std::floor(h) * alignment};

    xmax_media_size result{};
    double best_distance = std::numeric_limits<double>::infinity();
    for (uint32_t units = 1; units <= std::min(maximum_units, maximum_dimension_units); ++units) {
        const auto minimum_height = std::max(1u, (minimum_units + units - 1) / units);
        const auto maximum_height = std::min(maximum_units / units, maximum_dimension_units);
        if (minimum_height > maximum_height)
            continue;
        const auto height_units = std::clamp(std::round(h), double(minimum_height), double(maximum_height));
        const double dw = (units - w) / w;
        const double dh = (height_units - h) / h;
        const double distance = dw * dw + dh * dh;
        if (distance < best_distance) {
            best_distance = distance;
            result = {double(units * alignment), height_units * alignment};
        }
    }
    require(result.width > 0, XMAX_INVALID_ARGUMENT, "No aligned media size fits the limits");
    return result;
}

} // namespace

xmax_video_format MediaService::external_input(xmax_video_format requested, const xmax_model_info& model) {
    validate_format(requested);
    const auto size = external_size({double(requested.width), double(requested.height)}, model, 8192 / 4);
    return {uint32_t(size.width), uint32_t(size.height), requested.fps};
}

xmax_video_format MediaService::target(xmax_video_format source, double maximum_width, double maximum_height) {
    validate_format(source);
    const auto size = target_size({double(source.width), double(source.height)}, {maximum_width, maximum_height});
    return {uint32_t(size.width), uint32_t(size.height), source.fps};
}

xmax_media_size MediaService::media_size(xmax_media_size source, bool camera, const xmax_model_info& model) {
    if (!camera)
        return external_size(source, model, model.maximum_input_pixels / 16);

    for (uint32_t i = 0; i < model.format_count; ++i) {
        const auto& format = model.supported_formats[i];
        if (source.width == format.width && source.height == format.height)
            return source;
    }
    throw Error(XMAX_INVALID_ARGUMENT, "Camera target is not supported by the selected model");
}

xmax_media_size MediaService::generation_size(xmax_media_size source, const xmax_model_info& model) {
    require(positive_dimension(source.width) && positive_dimension(source.height),
            XMAX_INVALID_ARGUMENT, "Media dimensions must be positive and finite");
    const auto aspect = std::log(source.width) - std::log(source.height);
    auto best = model.supported_formats[0];
    double best_distance = std::numeric_limits<double>::infinity();
    for (uint32_t i = 0; i < model.format_count; ++i) {
        const auto& candidate = model.supported_formats[i];
        const auto distance = std::abs(aspect - std::log(double(candidate.width) / candidate.height));
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return {double(best.width), double(best.height)};
}

xmax_media_size MediaService::target_size(xmax_media_size source, xmax_media_size maximum) {
    const auto source_width = integral_dimension(source.width);
    const auto source_height = integral_dimension(source.height);
    require(positive_dimension(maximum.width) && positive_dimension(maximum.height),
            XMAX_INVALID_ARGUMENT, "Target dimensions must be positive and finite");
    const auto scale = std::min({1.0, maximum.width / source.width, maximum.height / source.height});
    const auto gcd = std::gcd(source_width, source_height);
    const auto exact = std::floor(double(gcd) * scale / 4) * 4;
    if (exact >= 4)
        return {double(source_width / gcd) * exact, double(source_height / gcd) * exact};
    const auto width = std::floor(source.width * scale / 4) * 4;
    const auto height = std::floor(source.height * scale / 4) * 4;
    require(width >= 4 && height >= 4, XMAX_INVALID_ARGUMENT, "Target bounds cannot fit a 4 by 4 frame");
    return {width, height};
}

} // namespace xmax::detail
