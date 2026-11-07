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
 * Runs independent typed storage transfers and maintains a snapshot of the most recently started transfer.
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
     * Invalidates all active transfers without waiting for network completion.
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
        bool safety,
        const TransferProgress& report = {},
        const Cancel& caller_cancelled = {},
        bool parallel = false
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
        }, report, caller_cancelled, parallel);
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
        bool safety,
        const TransferProgress& report = {},
        const Cancel& caller_cancelled = {},
        bool parallel = false
    ) {
        require(
            bytes && size,
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
        }, report, caller_cancelled, parallel);
    }

    /**
     * Downloads an HTTPS file and returns the completed destination size.
     *
     * @param[in] url An absolute HTTPS URL allowed by the transport's URL policy.
     * @param[in] path The UTF-8 destination path; must not name a directory.
     * @param[in] report The observer for this transfer's byte counts.
     * @param[in] caller_cancelled The caller's cooperative cancellation predicate.
     * @param[in] replace_existing Whether a successful download can replace the destination file.
     * @return The completed size, in bytes.
     */
    uint64_t download(
        const std::string& url,
        const std::string& path,
        const TransferProgress& report = {},
        const Cancel& caller_cancelled = {},
        bool replace_existing = false
    ) {
        return run(
            [&](const Cancel& cancel, const TransferProgress& progress) {
                return service_.download(
                    url,
                    path,
                    cancel,
                    progress,
                    replace_existing
                );
            },
            report,
            caller_cancelled,
            replace_existing
        );
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
     * The mutex protecting transfer state and byte progress.
     */
    mutable std::mutex progress_mutex_;

    /** The number of admitted transfers, including those awaiting cancellation cleanup. */
    size_t active_ = 0;

    /** Whether a legacy transfer currently holds exclusive controller-wide progress and cancellation. */
    bool exclusive_ = false;

    /** The identity of the transfer represented by the controller progress snapshot. */
    uint64_t latest_ = 0;

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
     * Releases transfer admission and updates the matching controller snapshot.
     *
     * @param[in] id The identity assigned on transfer admission.
     * @param[in] value The new XMAX_TRANSFER_* state.
     */
    void finish(
        uint64_t id,
        int32_t value
    ) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        if (--active_ == 0)
            exclusive_ = false;
        if (id == latest_)
            state_ = value;
    }

    /**
     * Admits a transfer and keeps its cancellation and progress independent of other typed transfers.
     *
     * @tparam F The callable type for the operation.
     *
     * @param[in] operation The transfer operation receiving cancellation and progress callbacks and
     * returning its result.
     * @param[in] report The per-transfer progress observer.
     * @param[in] caller_cancelled The per-transfer cancellation predicate.
     * @param[in] parallel Whether other typed transfers may overlap this transfer.
     *
     * @return The operation's result; failures and cancellation throw corresponding SDK exceptions.
     */
    template <class F>
    auto run(
        F operation,
        const TransferProgress& report,
        const Cancel& caller_cancelled,
        bool parallel
    ) -> decltype(operation(
        Cancel{},
        TransferProgress{}
    )) {
        uint64_t epoch = 0;
        const Cancel cancel = [this, &epoch, &caller_cancelled] {
            return epoch_.load() != epoch || (caller_cancelled && caller_cancelled());
        };
        uint64_t id = 0;
        const TransferProgress progress = [this, &report, &id](uint64_t bytes, uint64_t total) {
            {
                std::lock_guard<std::mutex> guard(progress_mutex_);
                if (id == latest_) {
                    transferred_ = bytes;
                    total_ = total;
                }
            }
            if (report)
                report(bytes, total);
        };
        {
            std::lock_guard<std::mutex> guard(progress_mutex_);
            require(!exclusive_ && (parallel || active_ == 0), XMAX_INVALID_STATE,
                    "A storage operation is already running");
            epoch = epoch_.load();
            ++active_;
            exclusive_ = !parallel;
            id = ++latest_;
            state_ = XMAX_TRANSFER_RUNNING;
            transferred_ = total_ = 0;
        }

        try {
            auto result = operation(
                cancel,
                progress
            );
            finish(id, XMAX_TRANSFER_SUCCEEDED);
            return result;
        } catch (const Error& error) {
            finish(id, error.code == XMAX_CANCELLED ? XMAX_TRANSFER_CANCELLED : XMAX_TRANSFER_FAILED);
            throw;
        } catch (...) {
            finish(id, XMAX_TRANSFER_FAILED);
            throw;
        }
    }
};
} // namespace xmax::detail
