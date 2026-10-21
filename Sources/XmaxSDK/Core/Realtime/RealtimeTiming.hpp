//
//  RealtimeTiming.hpp
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

namespace xmax::detail {

/**
 * 记录服务端会话、RTC 加入、本地发布和结果首帧的启动耗时。
 * 生命周期线程更新阶段记录；RTC 回调仅原子写入对应发布批次。
 */
class RealtimeTiming {

  public:

    /**
     * 在 RTC 回调与生命周期线程之间共享的单次发布记录。
     */
    struct Publication {

        /**
         * 首次实际发布的单调微秒时间，-1 表示尚未收到通知。
         */
        std::atomic<int64_t> first_us{-1};

        /**
         * 开始交付本地视频帧的单调微秒时间，-1 表示尚未开始。
         */
        int64_t started_us = -1;
    };

    /**
     * 单调时钟读取函数，可注入确定性时钟验证阶段时长。
     */
    using Now = std::function<Clock::time_point()>;

    /**
     * 保留单调时钟；注入函数应返回同一时间基准的时间点。
     *
     * @param[in] now 单调时钟读取函数，需支持生命周期线程调用。
     */
    explicit RealtimeTiming(
        Now now = [] { return Clock::now(); }
    )
        : now_(std::move(now)) {
    }

    /**
     * 开始一次连接启动计时，清除之前的阶段值。
     */
    void begin() {
        started_ = now_();
        snapshot_ = {};
        snapshot_.struct_size = sizeof(snapshot_);
        snapshot_.type = XMAX_EVENT_TIMING;
        snapshot_.session_ms = snapshot_.rtc_join_ms = snapshot_.publish_ms = snapshot_.first_frame_ms =
            snapshot_.total_ms = -1;
        publication_.reset();
    }

    /**
     * 记录服务端会话创建完成。
     */
    void finish_session() {
        snapshot_.session_ms = elapsed(started_, now_());
    }

    /**
     * 开始 RTC 加入计时，并分配独立的发布批次。
     */
    void begin_join() {
        joining_ = now_();
        publication_ = std::make_shared<Publication>();
    }

    /**
     * 开始本地帧交付，作为首次实际发布耗时的起点。
     */
    void begin_publication() {
        if (publication_)
            publication_->started_us = microseconds(now_());
    }

    /**
     * 记录 RTC 加入与本地交付建立完成。
     */
    void finish_join() {
        snapshot_.rtc_join_ms = elapsed(joining_, now_());
    }

    /**
     * 开始发送本次生成请求，等待该任务结果首帧。
     */
    void begin_signal() {
        signalling_ = now_();
    }

    /**
     * 记录结果首帧与总启动时长，并汇总已到达的发布通知。
     */
    void finish_signal() {
        const auto now = now_();
        snapshot_.first_frame_ms = elapsed(signalling_, now);
        snapshot_.total_ms = elapsed(started_, now);
        update_publication();
    }

    /**
     * 获取可由当前连接 RTC 回调持有的发布批次。
     */
    std::shared_ptr<Publication> publication() const {
        return publication_;
    }

    /**
     * 原子记录某批次的首次实际发布通知，重复通知不覆盖时间。
     *
     * @param[in] publication 回调创建时保留的发布批次。
     * @param[in] at 通知发生的单调时钟时间。
     * @return 本批次第一次收到通知时返回 true。
     */
    static bool published(
        const std::shared_ptr<Publication>& publication,
        Clock::time_point at
    ) {
        if (!publication)
            return false;
        int64_t missing = -1;
        return publication->first_us.compare_exchange_strong(missing, microseconds(at));
    }

    /**
     * 合并当前批次的首次发布通知，拒绝旧连接的批次。
     *
     * @param[in] publication 待检查的发布批次。
     * @return 新写入有效发布耗时时返回 true。
     */
    bool update_publication(
        const std::shared_ptr<Publication>& publication
    ) {
        if (publication != publication_ || snapshot_.publish_ms >= 0)
            return false;
        return update_publication();
    }

    /**
     * 获取当前阶段快照；尚未完成的阶段值为 -1，单位为毫秒。
     */
    xmax_event snapshot() const {
        return snapshot_;
    }

    /**
     * 释放当前发布批次，旧 RTC 回调不能修改后续连接记录。
     */
    void reset_publication() {
        publication_.reset();
    }

  private:

    /**
     * 当前计时使用的单调时钟。
     */
    Now now_;

    /**
     * 本次连接开始时间。
     */
    Clock::time_point started_{};

    /**
     * RTC 加入开始时间。
     */
    Clock::time_point joining_{};

    /**
     * 当前生成信令开始时间。
     */
    Clock::time_point signalling_{};

    /**
     * 最新的阶段耗时快照。
     */
    xmax_event snapshot_{};

    /**
     * 当前连接的发布通知记录。
     */
    std::shared_ptr<Publication> publication_;

    /**
     * 将单调时间点转换为微秒，用于跨线程原子通知。
     *
     * @param[in] at 单调时钟时间点。
     * @return 从单调时钟原点开始的微秒数。
     */
    static int64_t microseconds(
        Clock::time_point at
    ) {
        return std::chrono::duration_cast<std::chrono::microseconds>(at.time_since_epoch()).count();
    }

    /**
     * 计算阶段耗时，避免负时长。
     *
     * @param[in] begin 阶段开始时间。
     * @param[in] end 阶段完成时间。
     * @return 以毫秒为单位的非负耗时。
     */
    static double elapsed(
        Clock::time_point begin,
        Clock::time_point end
    ) {
        return std::max(0.0, std::chrono::duration<double, std::milli>(end - begin).count());
    }

    /**
     * 合并已经收到且具有起点的首次发布记录。
     *
     * @return 已写入有效发布耗时时返回 true。
     */
    bool update_publication() {
        if (!publication_ || publication_->started_us < 0)
            return false;
        const auto first = publication_->first_us.load();
        if (first < 0)
            return false;
        snapshot_.publish_ms = std::max(0.0, double(first - publication_->started_us) / 1000);
        return true;
    }
};
} // namespace xmax::detail
