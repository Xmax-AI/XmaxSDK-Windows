//
//  storage.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "handle_owner.hpp"
#include "error.hpp"
#include "uploaded_file.hpp"
#include <string>

namespace xmax {

/**
 * Provides cancellable synchronous uploads and downloads, with one transfer per instance at a time.
 */
class storage : public detail::owner<xmax_storage, xmax_storage_destroy> {

  public:

    /**
     * Inherits the handle owner's constructors; an empty handle creates an empty owner.
     *
     * @note The underlying resource is not copied. Destruction invokes its matching release function.
     */
    using owner::owner;

    /**
     * Requests active-transfer cancellation from any calling thread.
     *
     * @see xmax_storage_cancel
     */
    void cancel() const noexcept {
        xmax_storage_cancel(get());
    }

    /**
     * Returns a thread-safe transfer-progress snapshot.
     *
     * @return The transfer state and byte counts; an unknown total is 0.
     *
     * @see xmax_storage_get_progress
     */
    xmax_transfer_progress progress() const {
        xmax_transfer_progress value{};
        value.struct_size = sizeof(value);
        check(xmax_storage_get_progress(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Uploads a local encoded image or video on a worker thread.
     *
     * @param[in] path The UTF-8 path to an encoded local image or video file.
     * @param[in] check_safety Whether to perform a safety check when uploading an image.
     * @param[in] content_type An optional MIME type; empty infers the type from the filename.
     *
     * @return An owned upload result containing the URL, object key, and optional ETag.
     *
     * @see xmax_storage_upload_file
     */
    uploaded_file upload_file(
        const std::string& path,
        bool check_safety = false,
        const std::string& content_type = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            nullptr,
            content_type.empty() ? nullptr : content_type.c_str(),
            check_safety
        };
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_file(
            get(),
            path.c_str(),
            &options,
            &result
        ));

        return uploaded_file(result);
    }

    /**
     * Uploads encoded file bytes, not raw video pixels.
     *
     * @param[in] data The encoded bytes to process; they must remain valid until this synchronous call
     * returns.
     * @param[in] size The accessible or pending byte count.
     * @param[in] name The filename used for object metadata and MIME type inference.
     * @param[in] type An optional MIME type; empty infers the type from the filename.
     * @param[in] check_safety Whether to perform a safety check when uploading an image.
     *
     * @return An owned upload result containing the URL, object key, and optional ETag.
     *
     * @see xmax_storage_upload_data
     */
    uploaded_file upload_data(
        const uint8_t* data,
        size_t size,
        const std::string& name,
        const std::string& type = {},
        bool check_safety = false
    ) {
        const xmax_upload_options options{
            sizeof(options),
            name.c_str(),
            type.empty() ? nullptr : type.c_str(),
            check_safety
        };
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_data(
            get(),
            data,
            size,
            &options,
            &result
        ));

        return uploaded_file(result);
    }

    /**
     * Downloads HTTPS content to a new file without overwriting existing files.
     *
     * @param[in] url An absolute HTTPS URL allowed by the transport's URL policy.
     * @param[in] destination The UTF-8 destination path, which must not already exist.
     *
     * @return The successfully downloaded file size, in bytes.
     *
     * @see xmax_storage_download
     */
    uint64_t download(
        const std::string& url,
        const std::string& destination
    ) {
        uint64_t bytes = 0;
        check(xmax_storage_download(
            get(),
            url.c_str(),
            destination.c_str(),
            &bytes
        ));
        return bytes;
    }
};
} // namespace xmax
