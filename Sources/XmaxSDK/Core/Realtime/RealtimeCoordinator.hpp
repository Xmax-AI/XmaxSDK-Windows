//
//  RealtimeCoordinator.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace xmax::detail {

/**
 * 串行执行实时生命周期操作，管理取消批次、心跳期限及设有容量上限的事件队列。
 */
class RealtimeCoordinator {

  public:

    /**
     * 创建串行生命周期工作线程，并安装心跳和异步失败回调。
     *
     * @param[in] tick 生命周期心跳到期时执行的回调，在工作线程调用。
     * @param[in] failure 接收工作线程报告的异步错误，在工作线程触发。
     */
    explicit RealtimeCoordinator(
        std::function<void()> tick,
        std::function<void(Error)> failure = {}
    );

    /**
     * 排空并停止生命周期工作线程后释放成员资源。
     */
    ~RealtimeCoordinator();

    /**
     * 停止接收任务，排空队列并等待工作线程退出。
     */
    void shutdown();

    /**
     * 在生命周期队列执行操作，并将结果或异常同步传递给调用线程。
     *
     * @param[in] fn 提交到生命周期队列的操作，同步调用时异常返回到调用线程。
     */
    void sync(
        std::function<void()> fn
    );

    /**
     * 提交异步生命周期操作；执行器开始关闭后不再接收新任务。
     *
     * @param[in] fn 排入工作队列的操作，执行失败时通过错误回调报告。
     */
    void enqueue(
        std::function<void()> fn
    );

    /**
     * 原子读取当前取消批次。
     */
    uint64_t epoch() const {
        return epoch_.load();
    }

    /**
     * 使此前批次的操作失效，并返回新的取消批次。
     */
    uint64_t invalidate() {
        return ++epoch_;
    }

    /**
     * 为指定批次创建可跨线程调用的取消判断函数。
     *
     * @param[in] epoch 调用开始时捕获的取消批次，用于识别失效操作。
     * @return 返回 true 时表示该批次已取消的可调用对象。
     */
    Cancel cancellation(
        uint64_t epoch
    );

    /**
     * 读取当前生命周期状态，不等待执行队列。
     */
    xmax_state state() const {
        return state_.load();
    }

    /**
     * 原子发布新的生命周期状态。
     *
     * @param[in] value 新的实时生命周期状态。
     */
    void state(
        xmax_state value
    ) {
        state_ = value;
    }

    /**
     * 将事件加入应用队列，并按当前配置记录诊断信息；队列设有容量上限。
     *
     * @param[in] event 需要加入应用事件队列的事件。
     * @param[in] reason 当前状态或错误的业务原因。
     * @param[in] error 关联错误详情，调用时复制。
     */
    void
    emit(
        xmax_event event,
        xmax_realtime_reason reason = XMAX_REASON_NORMAL,
        const Error* error = nullptr
    );

    /**
     * 非阻塞取出最早的待处理应用事件。
     *
     * @param[out] event 接收最早的待处理应用事件，队列为空时保持原值。
     * @return 取得事件返回 true；队列为空返回 false。
     */
    bool poll(
        xmax_event& event
    );

    /**
     * 非阻塞取出带状态原因与错误详情的最早事件。
     *
     * @param[out] info 接收事件快照，队列为空时保持不变。
     * @return 是否取得事件；与旧事件轮询共用同一个队列。
     */
    bool poll(
        xmax_event_info& info
    );

    /**
     * 更新下一次心跳的单调时钟期限。
     *
     * @param[in] value 下一次心跳到期的单调时钟时间点。
     */
    void deadline(
        Clock::time_point value
    ) {
        deadline_ = value;
    }

    /**
     * 读取下一次心跳的单调时钟期限。
     */
    Clock::time_point deadline() const {
        return deadline_;
    }

  private:

    /**
     * 到期心跳处理函数，在生命周期工作线程调用。
     */
    std::function<void()> tick_;

    /**
     * 处理异步生命周期操作失败的回调，在工作线程调用。
     */
    std::function<void(Error)> failure_;

    /**
     * 串行处理生命周期操作的工作线程。
     */
    std::thread worker_;

    /**
     * 保护待执行任务队列及关闭标记的互斥锁。
     */
    std::mutex work_mutex_;

    /**
     * 保护应用事件队列的互斥锁。
     */
    std::mutex event_mutex_;

    /**
     * 通知工作线程有新任务、关闭请求或期限变更的条件变量。
     */
    std::condition_variable wake_;

    /**
     * 按提交顺序保存的生命周期操作队列。
     */
    std::deque<std::function<void()>> work_;

    /**
     * 供接入方非阻塞消费的设有容量上限的事件队列。
     */
    std::deque<xmax_event_info> events_;

    /**
     * 工作线程是否已开始关闭，受工作队列锁保护。
     */
    bool exiting_ = false;

    /**
     * 取消批次，递增后使旧批次操作失效。
     */
    std::atomic<uint64_t> epoch_{0};

    /**
     * 可跨线程原子读取的实时生命周期状态。
     */
    std::atomic<xmax_state> state_{XMAX_IDLE};

    /**
     * 下一次心跳到期的单调时钟时间点。
     */
    Clock::time_point deadline_ = Clock::time_point::max();

    /**
     * 串行处理生命周期任务和到期的心跳，关闭时排空已接收任务。
     */
    void run();
};
} // namespace xmax::detail
