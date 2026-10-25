//
//  QualityController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <optional>

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
);

/**
 * Manages directional quality levels, stability windows, cooldowns, and upgrade rollback.
 */
class QualityDirection {

  public:

    /**
     * The directional quality level, in [1, 5].
     */
    int level = 2;

    /**
     * Observes timestamped network samples and recommends levels after stability and cooldown checks.
     *
     * @param[in] quality The native network-quality value for this direction.
     * @param[in] sampled_at The sample's monotonic time in seconds; expired or duplicate samples are
     * ignored.
     * @param[in] now The current monotonic time in seconds, used for cooldowns, stable windows, and sample
     * expiry.
     *
     * @return A proposed level satisfying stability, freshness, and cooldown requirements; otherwise
     * std::nullopt.
     */
    std::optional<int> observe(
        int quality,
        double sampled_at,
        double now
    );

    /**
     * Records a level-change result and updates cooldown and failed-upgrade rollback windows.
     *
     * @param[in] next The attempted quality level, in [1, 5].
     * @param[in] success Whether native encoding or downlink quality changes succeeded.
     * @param[in] now The current monotonic time in seconds, used for cooldowns, stable windows, and sample
     * expiry.
     */
    void complete(
        int next,
        bool success,
        double now
    );

    /**
     * Clears the network observation window while retaining the current quality level.
     */
    void reset_samples();

  private:

    /**
     * The consecutive good network-quality sample count.
     */
    int good_ = 0;

    /**
     * The consecutive poor network-quality sample count.
     */
    int poor_ = 0;

    /**
     * The last accepted sample time in monotonic seconds; negative before the first sample.
     */
    double last_ = -1;

    /**
     * The current level's stability deadline, in monotonic seconds.
     */
    double stable_until_ = 0;

    /**
     * The next permitted quality-upgrade time, in monotonic seconds.
     */
    double upgrade_after_ = 0;

    /**
     * The upgrade probe deadline, in monotonic seconds.
     */
    double probe_until_ = 0;
};

/**
 * Manages independent uplink and downlink quality under a shared adaptation setting.
 */
class QualityController {

  public:

    /**
     * The uplink network observations and level policy.
     */
    QualityDirection up;

    /**
     * The downlink network observations and level policy.
     */
    QualityDirection down;

    /**
     * Whether independent directional quality adaptation is enabled.
     */
    bool enabled = true;

    /**
     * Resets uplink and downlink network observation windows.
     */
    void reset_samples() {
        up.reset_samples();
        down.reset_samples();
    }
};
} // namespace xmax::detail
