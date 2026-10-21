//
//  TrajectoryRegistry.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * 保存当前任务的短时轨迹快照，支持原生效果和接入方自定义绘制。
 */
class TrajectoryRegistry {

  public:

    /**
     * 保存本次采样，并在 600 毫秒后自动失效。
     *
     * @param[in] points 归一化轨迹，最多 256 个点。
     */
    void update(
        std::vector<xmax_point> points
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        points_ = std::move(points);
        expires_ = Clock::now() + std::chrono::milliseconds(600);
    }

    /**
     * 获取尚未过期的轨迹副本；没有有效轨迹时为空。
     */
    std::vector<xmax_point> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return Clock::now() < expires_ ? points_ : std::vector<xmax_point>{};
    }

    /**
     * 任务停止或切换时立即移除轨迹。
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        points_.clear();
        expires_ = {};
    }

  private:

    /**
     * 保护轨迹缓存的互斥锁。
     */
    mutable std::mutex mutex_;

    /**
     * 当前任务最新的归一化轨迹。
     */
    std::vector<xmax_point> points_;

    /**
     * 轨迹显示到期时间，使用单调时钟。
     */
    Clock::time_point expires_{};
};
} // namespace xmax::detail
