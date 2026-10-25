//
//  QualityController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Quality/QualityController.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace xmax::detail {

/**
 * Resolves video dimensions and frame rate for a directional quality level.
 *
 * @param[in] baseline The bound input's baseline video format.
 * @param[in] level The quality level, in [1, 5].
 * @param[in] downlink True for downlink; false for uplink.
 */
xmax_video_format quality_format(
    xmax_video_format baseline,
    int level,
    bool downlink
) {
    static const double scales[]{1, .8, .65, .5, .5};
    static const uint32_t rates[]{30, 24, 24, 20, 16};
    const auto index = std::clamp(level, 1, 5) - 1;
    const auto short_side = std::min(baseline.width, baseline.height);
    const auto scale = std::max(scales[index], double(std::min(short_side, 240u)) / short_side);
    const auto alignment = downlink ? 4u : 2u;
    const auto divisor = std::gcd(
        baseline.width,
        baseline.height
    );
    const auto units = uint32_t(std::floor(divisor * scale + 1e-7)) / alignment * alignment;
    xmax_video_format result{};

    if (units >= alignment) {
        result.width = baseline.width / divisor * units;
        result.height = baseline.height / divisor * units;
    } else {
        result.width =
            std::max(alignment, uint32_t(std::floor(baseline.width * scale / alignment)) * alignment);
        result.height =
            std::max(alignment, uint32_t(std::floor(baseline.height * scale / alignment)) * alignment);
    }

    result.fps = downlink ? 24 : std::min(baseline.fps, rates[index]);

    return result;
}

/**
 * Clears the network observation window while retaining the current quality level.
 */
void QualityDirection::reset_samples() {
    good_ = poor_ = 0;
    last_ = -1;
}

/**
 * Observes timestamped network samples and recommends levels after stability and cooldown checks.
 *
 * @param[in] quality The native network-quality value for this direction.
 * @param[in] sampled_at The sample's monotonic time in seconds; expired or duplicate samples are ignored.
 * @param[in] now The current monotonic time in seconds, used for cooldowns, stable windows, and sample
 * expiry.
 *
 * @return A proposed level satisfying stability, freshness, and cooldown requirements; otherwise
 * std::nullopt.
 */
std::optional<int> QualityDirection::observe(
    int quality,
    double sampled_at,
    double now
) {
    if (!std::isfinite(sampled_at) || !std::isfinite(now) || sampled_at > now ||
        (last_ >= 0 && sampled_at <= last_))
        return {};

    if (last_ >= 0 && sampled_at - last_ > 5)
        reset_samples();

    last_ = sampled_at;

    if (now - sampled_at > 5 || now < stable_until_) {
        good_ = poor_ = 0;
        return {};
    }

    if (quality == 4 || quality == 5) {
        good_ = 0;
        if (++poor_ >= 3 && level < 5)
            return level + 1;
    } else if (quality == 1 || quality == 2) {
        poor_ = 0;
        if (++good_ >= 5 && now >= upgrade_after_ && level > 1)
            return level - 1;
    } else
        good_ = poor_ = 0;

    return {};
}

/**
 * Records a level-change result and updates cooldown and failed-upgrade rollback windows.
 *
 * @param[in] next The attempted quality level, in [1, 5].
 * @param[in] success Whether native encoding or downlink quality changes succeeded.
 * @param[in] now The current monotonic time in seconds, used for cooldowns, stable windows, and sample
 * expiry.
 */
void QualityDirection::complete(
    int next,
    bool success,
    double now
) {
    reset_samples();
    stable_until_ = now + 4;

    if (!success)
        return;

    if (next < level)
        probe_until_ = now + 20;
    else if (next > level) {
        if (now < probe_until_)
            upgrade_after_ = now + 60;
        probe_until_ = 0;
    }

    level = next;
}
} // namespace xmax::detail
