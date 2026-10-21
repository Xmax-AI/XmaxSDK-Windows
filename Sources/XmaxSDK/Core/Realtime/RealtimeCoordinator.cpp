//
//  RealtimeCoordinator.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/RealtimeCoordinator.hpp"
#include "Foundation/Logging/RtcStatsLogger.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {

/**
 * 创建串行生命周期工作线程，并安装心跳和异步失败回调。
 *
 * @param[in] tick 生命周期心跳到期时执行的回调，在工作线程调用。
 * @param[in] failure 接收工作线程报告的异步错误，在工作线程触发。
 */
RealtimeCoordinator::RealtimeCoordinator(
    std::function<void()> tick,
    std::function<void(Error)> failure
)
    : tick_(std::move(tick)), failure_(std::move(failure)) {
    worker_ = std::thread([this] { run(); });
}

/**
 * 排空并停止生命周期工作线程后释放成员资源。
 */
RealtimeCoordinator::~RealtimeCoordinator() {
    shutdown();
}

/**
 * 停止接收任务，排空队列并等待工作线程退出。
 */
void RealtimeCoordinator::shutdown() {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        exiting_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

/**
 * 提交异步生命周期操作；执行器开始关闭后不再接收新任务。
 *
 * @param[in] fn 排入工作队列的操作，执行失败时通过错误回调报告。
 */
void RealtimeCoordinator::enqueue(
    std::function<void()> fn
) {
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        work_.push_back(std::move(fn));
    }
    wake_.notify_one();
}

/**
 * 在生命周期队列执行操作，并将结果或异常同步传递给调用线程。
 *
 * @param[in] fn 提交到生命周期队列的操作，同步调用时异常返回到调用线程。
 */
void RealtimeCoordinator::sync(
    std::function<void()> fn
) {
    auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
    auto result = task->get_future();
    enqueue([task] { (*task)(); });
    result.get();
}

/**
 * 串行处理生命周期任务和到期的心跳，关闭时排空已接收任务。
 */
void RealtimeCoordinator::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(work_mutex_);
            wake_.wait_until(lock, deadline_, [this] { return exiting_ || !work_.empty(); });

            if (exiting_ && work_.empty())
                return;

            if (!work_.empty()) {
                job = std::move(work_.front());
                work_.pop_front();
            }
        }
        try {
            if (job)
                job();
            if (Clock::now() >= deadline_)
                tick_();
        } catch (...) {
            deadline_ = Clock::time_point::max();
            // 隔离异步回调异常，确保工作线程持续处理后续同步操作。
            try {
                if (failure_)
                    failure_(Error(XMAX_INTERNAL_ERROR, "An asynchronous SDK operation failed"));
                else {
                    xmax_event event{};
                    event.type = XMAX_EVENT_ERROR;
                    event.code = XMAX_INTERNAL_ERROR;
                    emit(event);
                }
            } catch (...) {
            }
        }
    }
}

/**
 * 为指定批次创建可跨线程调用的取消判断函数。
 *
 * @param[in] epoch 调用开始时捕获的取消批次，用于识别失效操作。
 * @return 返回 true 时表示该批次已取消的可调用对象。
 */
Cancel RealtimeCoordinator::cancellation(
    uint64_t epoch
) {
    return [this, epoch] { return epoch_.load() != epoch; };
}

/**
 * 将事件加入应用队列，并按当前配置记录诊断信息；队列设有容量上限。
 *
 * @param[in] event 需要加入应用事件队列的事件。
 * @param[in] reason 当前状态或错误的业务原因。
 * @param[in] error 关联错误详情，调用时复制。
 */
void RealtimeCoordinator::emit(
    xmax_event event,
    xmax_realtime_reason reason,
    const Error* error
) {
    RtcStatsLogger::event(event);

    if (event.type == XMAX_EVENT_ERROR)
        XmaxLogger::write(LogCategory::Realtime, XMAX_LOG_ERROR, [&](auto language) {
            return XmaxLogger::localized(
                       language, "实时操作失败 (Realtime Operation Failed)", "Realtime Operation Failed"
                   ) +
                   "\n├─ code=" + std::to_string(event.code) + ", session=" + event.session_id +
                   ", task=" + event.task_id + "\n└─ " + std::string(event.message);
        });

    event.struct_size = sizeof(event);

    std::lock_guard<std::mutex> lock(event_mutex_);

    if (events_.size() >= 256) {
        auto old = std::find_if(events_.begin(), events_.end(), [](const auto& e) {
            return e.event.type == XMAX_EVENT_STATISTICS;
        });
        if (old != events_.end())
            events_.erase(old);
        else
            events_.pop_front();
    }

    xmax_event_info info{};
    info.struct_size = sizeof(info);
    info.event = event;
    info.reason = reason;
    info.error.struct_size = sizeof(info.error);
    if (error) {
        info.error.code = error->code;
        info.error.api_code = error->api_code;
        info.error.http_status = error->http_status;
        const auto safe = XmaxLogger::redact(error->what());
        auto length = std::min(safe.size(), sizeof(info.error.message) - 1);
        while (length && length < safe.size() && (static_cast<unsigned char>(safe[length]) & 0xc0) == 0x80)
            --length;
        std::memcpy(info.error.message, safe.data(), length);
    }
    events_.push_back(info);
}

/**
 * 非阻塞取出最早的待处理应用事件。
 *
 * @param[out] event 接收最早的待处理应用事件，队列为空时保持原值。
 * @return 取得事件返回 true；队列为空返回 false。
 */
bool RealtimeCoordinator::poll(
    xmax_event& event
) {
    xmax_event_info info{};
    if (!poll(info))
        return false;
    event = info.event;
    return true;
}

/**
 * 非阻塞取出带状态原因与错误详情的最早事件。
 *
 * @param[out] info 接收事件快照，队列为空时保持不变。
 * @return 是否取得事件。
 */
bool RealtimeCoordinator::poll(
    xmax_event_info& info
) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.empty())
        return false;
    info = events_.front();
    events_.pop_front();
    return true;
}
} // namespace xmax::detail
