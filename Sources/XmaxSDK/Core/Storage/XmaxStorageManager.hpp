//
//  XmaxStorageManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Storage/StorageService.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * 管理单个存储传输、跨线程取消和进度快照；同一实例不接受并行传输。
 */
class XmaxStorageManager {

  public:

    /**
     * 使用共享 API 服务创建存储服务，并独立管理取消状态和传输进度。
     *
     * @param[in] api 共享 API 服务，控制器在其生命周期内保留该引用。
     */
    explicit XmaxStorageManager(
        std::shared_ptr<ApiService> api
    )
        : service_(std::move(api)) {
    }

    /**
     * 使在途传输的取消批次失效，不等待网络操作退出。
     */
    void cancel() {
        ++epoch_;
    }

    /**
     * 从任意接入线程读取受锁保护的传输进度快照。
     */
    xmax_transfer_progress progress() const {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        return {sizeof(xmax_transfer_progress), state_, transferred_, total_};
    }

    /**
     * 上传本地编码文件，并维护传输进度与取消状态。
     *
     * @param[in] path UTF-8 本地编码图片或视频路径。
     * @param[in] name 文件名称，用于对象元数据及 MIME 类型推断。
     * @param[in] type 可选 MIME 类型，空文本按文件名推断。
     * @param[in] safety 是否对图片上传执行安全检查。
     * @return 独立拥有的上传结果，包含文件访问地址、对象键和可选 ETag。
     */
    StoredFile upload_file(
        const std::string& path,
        const std::string& name,
        const std::string& type,
        bool safety
    ) {
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.upload_file(path, name, type, safety, cancel, progress);
        });
    }

    /**
     * 复制编码文件字节并执行上传，维护传输进度与取消状态。
     *
     * @param[in] bytes 待处理的编码字节，本次同步调用返回前必须有效。
     * @param[in] size 可访问或待处理的字节数。
     * @param[in] name 文件名称，用于对象元数据及 MIME 类型推断。
     * @param[in] type 可选 MIME 类型，空文本按文件名推断。
     * @param[in] safety 是否对图片上传执行安全检查。
     * @return 独立拥有的上传结果，包含文件访问地址、对象键和可选 ETag。
     */
    StoredFile upload_data(
        const uint8_t* bytes,
        size_t size,
        const std::string& name,
        const std::string& type,
        bool safety
    ) {
        require(
            bytes && size && size <= 256 * 1024 * 1024, XMAX_INVALID_ARGUMENT, "Invalid upload data range"
        );
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.upload(
                std::string(reinterpret_cast<const char*>(bytes), size), name, type, safety, cancel, progress
            );
        });
    }

    /**
     * 下载 HTTPS 文件并返回已发布文件的字节数。
     *
     * @param[in] url 完整 HTTPS 地址，需满足当前传输接口的地址策略。
     * @param[in] path UTF-8 本地目标文件路径，目标必须尚不存在。
     */
    uint64_t download(
        const std::string& url,
        const std::string& path
    ) {
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.download(url, path, cancel, progress);
        });
    }

  private:

    /**
     * 执行实际上传、下载及安全检查的存储服务。
     */
    StorageService service_;

    /**
     * 取消批次，递增后使旧批次操作失效。
     */
    std::atomic<uint64_t> epoch_{0};

    /**
     * 确保同一存储控制器只运行一次传输的互斥锁。
     */
    std::mutex operation_mutex_;

    /**
     * 保护传输状态和进度字节数的互斥锁。
     */
    mutable std::mutex progress_mutex_;

    /**
     * 传输状态，使用 XMAX_TRANSFER_* 常量并由进度锁保护。
     */
    int32_t state_ = XMAX_TRANSFER_IDLE;

    /**
     * 当前传输已完成的字节数，受进度锁保护。
     */
    uint64_t transferred_ = 0;

    /**
     * 当前传输总字节数，未知时为 0，受进度锁保护。
     */
    uint64_t total_ = 0;

    /**
     * 在进度锁保护下更新传输的最终状态。
     *
     * @param[in] value 新的 XMAX_TRANSFER_* 传输状态。
     */
    void state(
        int32_t value
    ) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        state_ = value;
    }

    /**
     * 串行执行一次传输，将完成、取消或失败映射到可查询状态。
     *
     * @tparam F 待执行操作的可调用对象类型。
     * @param[in] operation 接受取消判断和进度回调的传输操作，返回实际传输结果。
     * @return 传输操作的返回值；失败或取消时抛出对应 SDK 异常。
     */
    template <class F>
    auto run(
        F operation
    ) -> decltype(operation(Cancel{}, TransferProgress{})) {
        std::unique_lock<std::mutex> lock(operation_mutex_, std::try_to_lock);
        require(lock.owns_lock(), XMAX_INVALID_STATE, "A storage operation is already running");

        const auto epoch = epoch_.load();
        const Cancel cancel = [this, epoch] { return epoch_.load() != epoch; };
        const TransferProgress progress = [this](uint64_t bytes, uint64_t total) {
            std::lock_guard<std::mutex> guard(progress_mutex_);
            transferred_ = bytes;
            total_ = total;
        };
        {
            std::lock_guard<std::mutex> guard(progress_mutex_);
            state_ = XMAX_TRANSFER_RUNNING;
            transferred_ = total_ = 0;
        }

        try {
            auto result = operation(cancel, progress);
            state(XMAX_TRANSFER_SUCCEEDED);
            return result;
        } catch (const Error& error) {
            state(error.code == XMAX_CANCELLED ? XMAX_TRANSFER_CANCELLED : XMAX_TRANSFER_FAILED);
            throw;
        } catch (...) {
            state(XMAX_TRANSFER_FAILED);
            throw;
        }
    }
};
} // namespace xmax::detail
