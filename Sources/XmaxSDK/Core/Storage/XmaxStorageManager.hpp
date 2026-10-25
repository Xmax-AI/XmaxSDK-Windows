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
 * Manages one storage transfer with cross-thread cancellation and progress snapshots; parallel transfers on
 * one instance are rejected.
 */
class XmaxStorageManager {

  public:

    /**
     * Creates a storage service with shared API access and independent cancellation and progress state.
     *
     * @param[in] api The shared API service, retained for the controller's lifetime.
     */
    explicit XmaxStorageManager(
        std::shared_ptr<ApiService> api
    )
        : service_(std::move(api)) {
    }

    /**
     * Invalidates the active transfer's cancellation revision without waiting for network completion.
     */
    void cancel() {
        ++epoch_;
    }

    /**
     * Returns a locked progress snapshot from any calling thread.
     */
    xmax_transfer_progress progress() const {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        return {
            sizeof(xmax_transfer_progress),
            state_,
            transferred_,
            total_
        };
    }

    /**
     * Uploads a local encoded file while tracking progress and cancellation.
     *
     * @param[in] path The UTF-8 path to an encoded local image or video file.
     * @param[in] name The filename used for object metadata and MIME type inference.
     * @param[in] type An optional MIME type; empty infers the type from the filename.
     * @param[in] safety Whether to perform a safety check when uploading an image.
     *
     * @return An owned upload result containing the URL, object key, and optional ETag.
     */
    StoredFile upload_file(
        const std::string& path,
        const std::string& name,
        const std::string& type,
        bool safety
    ) {
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.upload_file(
                path,
                name,
                type,
                safety,
                cancel,
                progress
            );
        });
    }

    /**
     * Copies and uploads encoded bytes while tracking progress and cancellation.
     *
     * @param[in] bytes The encoded bytes to process; they must remain valid until this synchronous call
     * returns.
     * @param[in] size The accessible or pending byte count.
     * @param[in] name The filename used for object metadata and MIME type inference.
     * @param[in] type An optional MIME type; empty infers the type from the filename.
     * @param[in] safety Whether to perform a safety check when uploading an image.
     *
     * @return An owned upload result containing the URL, object key, and optional ETag.
     */
    StoredFile upload_data(
        const uint8_t* bytes,
        size_t size,
        const std::string& name,
        const std::string& type,
        bool safety
    ) {
        require(
            bytes && size && size <= 256 * 1024 * 1024,
            XMAX_INVALID_ARGUMENT,
            "Invalid upload data range"
        );

        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.upload(
                std::string(
                    reinterpret_cast<const char*>(bytes),
                    size
                ),
                name,
                type,
                safety,
                cancel,
                progress
            );
        });
    }

    /**
     * Downloads an HTTPS file and returns the completed destination size.
     *
     * @param[in] url An absolute HTTPS URL allowed by the transport's URL policy.
     * @param[in] path The UTF-8 destination path; the file must not already exist.
     */
    uint64_t download(
        const std::string& url,
        const std::string& path
    ) {
        return run([&](const Cancel& cancel, const TransferProgress& progress) {
            return service_.download(
                url,
                path,
                cancel,
                progress
            );
        });
    }

  private:

    /**
     * The storage service performing uploads, downloads, and safety checks.
     */
    StorageService service_;

    /**
     * The cancellation revision; incrementing it invalidates earlier work.
     */
    std::atomic<uint64_t> epoch_{0};

    /**
     * The mutex limiting this storage controller to one transfer at a time.
     */
    std::mutex operation_mutex_;

    /**
     * The mutex protecting transfer state and byte progress.
     */
    mutable std::mutex progress_mutex_;

    /**
     * The XMAX_TRANSFER_* state, protected by the progress mutex.
     */
    int32_t state_ = XMAX_TRANSFER_IDLE;

    /**
     * The completed byte count, protected by the progress mutex.
     */
    uint64_t transferred_ = 0;

    /**
     * The total byte count, or 0 if unknown, protected by the progress mutex.
     */
    uint64_t total_ = 0;

    /**
     * Updates the final transfer state under the progress mutex.
     *
     * @param[in] value The new XMAX_TRANSFER_* state.
     */
    void state(
        int32_t value
    ) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        state_ = value;
    }

    /**
     * Serializes a transfer and maps completion, cancellation, or failure to queryable state.
     *
     * @tparam F The callable type for the operation.
     *
     * @param[in] operation The transfer operation receiving cancellation and progress callbacks and
     * returning its result.
     *
     * @return The operation's result; failures and cancellation throw corresponding SDK exceptions.
     */
    template <class F>
    auto run(
        F operation
    ) -> decltype(operation(
        Cancel{},
        TransferProgress{}
    )) {
        std::unique_lock<std::mutex> lock(
            operation_mutex_,
            std::try_to_lock
        );
        require(
            lock.owns_lock(),
            XMAX_INVALID_STATE,
            "A storage operation is already running"
        );

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
            auto result = operation(
                cancel,
                progress
            );
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
