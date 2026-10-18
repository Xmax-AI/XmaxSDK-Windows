#pragma once
#include "Service/Storage/StorageService.hpp"
#include <mutex>
namespace xmax::detail {
class XmaxStorageManager {
  public:
    explicit XmaxStorageManager(std::shared_ptr<ApiService> api) : service_(std::move(api)) {}
    void cancel() { ++epoch_; }
    xmax_transfer_progress progress() const {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        return {sizeof(xmax_transfer_progress), state_, transferred_, total_};
    }
    StoredFile upload_file(const std::string& path, const std::string& name, const std::string& type,
                           bool safety) {
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.upload_file(path, name, type, safety, cancel, progress);
        });
    }
    StoredFile upload_data(const uint8_t* bytes, size_t size, const std::string& name,
                           const std::string& type, bool safety) {
        require(bytes && size && size <= 256 * 1024 * 1024, XMAX_INVALID_ARGUMENT,
                "Invalid upload data range");
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.upload(std::string(reinterpret_cast<const char*>(bytes), size), name, type,
                                   safety, cancel, progress);
        });
    }
    uint64_t download(const std::string& url, const std::string& path) {
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.download(url, path, cancel, progress);
        });
    }

  private:
    StorageService service_;
    std::atomic<uint64_t> epoch_{0};
    std::mutex operation_mutex_;
    mutable std::mutex progress_mutex_;
    int32_t state_ = XMAX_TRANSFER_IDLE;
    uint64_t transferred_ = 0, total_ = 0;
    void state(int32_t value) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        state_ = value;
    }
    template <class F> auto run(F operation) -> decltype(operation(Cancel{}, TransferProgress{})) {
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
