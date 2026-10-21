//
//  InteractionController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include "Media/Interaction/InteractionCoordinateMapper.hpp"
#include <mutex>
#include <optional>

namespace xmax::detail {

/**
 * 合并待发送轨迹采样；调用线程仅保存最新点集，不等待 RTC 发送。
 */
class InteractionController {

  public:

    /**
     * 一次采样所属的生命周期批次和点集。
     */
    struct Sample {

        /**
         * 提交时的连接取消批次，用于拒收旧连接采样。
         */
        uint64_t epoch;

        /**
         * 提交时的生成任务，切换摄像头后不得发送到新任务。
         */
        std::string task;

        /**
         * 已映射的归一化点集。
         */
        std::vector<xmax_point> points;
    };

    /**
     * 替换尚未发送的采样，并申请最多一个待执行发送任务。
     *
     * @param[in] sample 已复制且携带任务身份的采样。
     * @return true 表示调用方应安排一次发送任务。
     */
    bool submit(
        Sample sample
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::move(sample);
        if (scheduled_)
            return false;
        scheduled_ = true;
        return true;
    }

    /**
     * 取出最新采样；无采样时释放任务预约，允许下一次提交再次预约。
     */
    std::optional<Sample> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto value = std::exchange(pending_, {});
        if (!value)
            scheduled_ = false;
        return value;
    }

    /**
     * 清除尚未发送的采样；已预约任务仍负责释放预约。
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
    }

  private:

    /**
     * 保护最新采样与任务预约的互斥锁。
     */
    std::mutex mutex_;

    /**
     * 尚未发送的最新采样，旧采样可被替换。
     */
    std::optional<Sample> pending_;

    /**
     * 是否已经有一个发送任务在执行或等待执行。
     */
    bool scheduled_ = false;
};
} // namespace xmax::detail
