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
);

/**
 * 管理一个传输方向的画质档位、稳定窗口、冷却及升级回退策略。
 */
class QualityDirection {

  public:

    /**
     * 当前方向的画质档位，范围为 [1, 5]。
     */
    int level = 2;

    /**
     * 观察带时间戳的网络质量样本，在满足稳定与冷却条件时建议新档位。
     *
     * @param[in] quality 原生网络质量枚举值，用于当前方向的档位观察。
     * @param[in] sampled_at 网络样本产生时的单调秒时间，过期或重复样本不参与决策。
     * @param[in] now 当前单调秒时间，用于冷却、稳定窗口及样本时效判断。
     * @return 符合稳定、样本时效及冷却条件的新档位，否则返回 std::nullopt。
     */
    std::optional<int> observe(
        int quality,
        double sampled_at,
        double now
    );

    /**
     * 记录档位变更结果，更新冷却期限及失败升级的回退窗口。
     *
     * @param[in] next 本次尝试应用的画质档位，范围为 [1, 5]。
     * @param[in] success 原生编码或下行画质变更是否已成功应用。
     * @param[in] now 当前单调秒时间，用于冷却、稳定窗口及样本时效判断。
     */
    void complete(
        int next,
        bool success,
        double now
    );

    /**
     * 清空网络观察窗口，保留当前画质档位。
     */
    void reset_samples();

  private:

    /**
     * 连续良好网络质量样本数量。
     */
    int good_ = 0;

    /**
     * 连续较差网络质量样本数量。
     */
    int poor_ = 0;

    /**
     * 最近一次接受的网络样本单调秒时间，负值表示尚无样本。
     */
    double last_ = -1;

    /**
     * 当前画质档位稳定观察期限，使用单调秒时间。
     */
    double stable_until_ = 0;

    /**
     * 下一次允许升级画质的单调秒时间。
     */
    double upgrade_after_ = 0;

    /**
     * 升级探测窗口结束的单调秒时间。
     */
    double probe_until_ = 0;
};

/**
 * 分别管理上行与下行画质，并统一控制自适应开关。
 */
class QualityController {

  public:

    /**
     * 上行网络质量观察与档位策略。
     */
    QualityDirection up;

    /**
     * 下行网络质量观察与档位策略。
     */
    QualityDirection down;

    /**
     * 是否启用独立的双向画质自适应。
     */
    bool enabled = true;

    /**
     * 重置上行与下行的网络观察窗口。
     */
    void reset_samples() {
        up.reset_samples();
        down.reset_samples();
    }
};
} // namespace xmax::detail
