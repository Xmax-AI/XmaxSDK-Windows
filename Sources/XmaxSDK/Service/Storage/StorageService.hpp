//
//  StorageService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Storage/StoredFile.hpp"
#include "Service/Network/ApiService.hpp"

namespace xmax::detail {

/**
 * The progress callback receiving completed bytes then total bytes; unknown totals are 0.
 */
using TransferProgress = std::function<void(uint64_t, uint64_t)>;

/**
 * Obtains temporary credentials, uploads files, optionally checks image safety, and performs bounded
 * downloads.
 */
class StorageService {

  public:

    /**
     * Retains shared API services for temporary credentials, safety checks, and file transfers.
     */
    explicit StorageService(
        std::shared_ptr<ApiService> api
    )
        : api_(std::move(api)) {
    }

    /**
     * Uploads encoded bytes with temporary storage credentials and an optional image safety check.
     *
     * @param[in] data The encoded bytes to process; they must remain valid until this synchronous call
     * returns.
     * @param[in] name The filename used for object metadata and MIME type inference.
     * @param[in] content_type An optional MIME type; empty infers the type from the filename.
     * @param[in] safety Whether to perform a safety check when uploading an image.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] progress The byte-progress callback, receiving completed bytes followed by total bytes.
     *
     * @return An owned upload result containing the URL, object key, and optional ETag.
     */
    StoredFile upload(
        const std::string& data,
        std::string name,
        std::string content_type,
        bool safety,
        const Cancel& cancel,
        const TransferProgress& progress
    );

    /**
     * Reads a local encoded image or video and uploads it using the supplied metadata.
     *
     * @param[in] path The UTF-8 path to an encoded local image or video file.
     * @param[in] name The filename used for object metadata and MIME type inference.
     * @param[in] type An optional MIME type; empty infers the type from the filename.
     * @param[in] safety Whether to perform a safety check when uploading an image.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] progress The byte-progress callback, receiving completed bytes followed by total bytes.
     *
     * @return An owned upload result containing the URL, object key, and optional ETag.
     */
    StoredFile upload_file(
        const std::string& path,
        std::string name,
        std::string type,
        bool safety,
        const Cancel& cancel,
        const TransferProgress& progress
    );

    /**
     * Downloads HTTPS content to a temporary file and publishes it to a nonexistent destination on success.
     *
     * @param[in] url An absolute HTTPS URL allowed by the transport's URL policy.
     * @param[in] path The UTF-8 destination path; the file must not already exist.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     * @param[in] progress The byte-progress callback, receiving completed bytes followed by total bytes.
     *
     * @return The size of the completed destination file, in bytes.
     */
    uint64_t download(
        const std::string& url,
        const std::string& path,
        const Cancel& cancel,
        const TransferProgress& progress
    );

    /**
     * Infers a supported image or video MIME type from the filename extension.
     *
     * @param[in] name The filename used for object metadata and MIME type inference.
     *
     * @return A supported MIME type; unsupported extensions throw an SDK error.
     */
    static std::string content_type(
        const std::string& name
    );

  private:

    /**
     * Retains the shared API service so child controllers survive destruction of the client handle.
     */
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
