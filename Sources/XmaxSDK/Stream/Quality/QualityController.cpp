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
 * 将画质档位解析为指定方向的视频尺寸和帧率。
 *
 * @param[in] baseline 绑定输入的基准视频规格。
 * @param[in] level 画质档位，范围为 [1, 5]。
 * @param[in] downlink true 表示下行，false 表示上行。
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
    const auto divisor = std::gcd(baseline.width, baseline.height);
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
 * 清空网络观察窗口，保留当前画质档位。
 */
void QualityDirection::reset_samples() {
    good_ = poor_ = 0;
    last_ = -1;
}

/**
 * 观察带时间戳的网络质量样本，在满足稳定与冷却条件时建议新档位。
 *
 * @param[in] quality 原生网络质量枚举值，用于当前方向的档位观察。
 * @param[in] sampled_at 网络样本产生时的单调秒时间，过期或重复样本不参与决策。
 * @param[in] now 当前单调秒时间，用于冷却、稳定窗口及样本时效判断。
 * @return 符合稳定、样本时效及冷却条件的新档位，否则返回 std::nullopt。
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
 * 记录档位变更结果，更新冷却期限及失败升级的回退窗口。
 *
 * @param[in] next 本次尝试应用的画质档位，范围为 [1, 5]。
 * @param[in] success 原生编码或下行画质变更是否已成功应用。
 * @param[in] now 当前单调秒时间，用于冷却、稳定窗口及样本时效判断。
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
