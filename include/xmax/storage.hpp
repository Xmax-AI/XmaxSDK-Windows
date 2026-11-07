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
#include "downloaded_file.hpp"
#include "storage_transfer_callbacks.hpp"
#include <string>

namespace xmax {

/**
 * Provides cancellable synchronous uploads and downloads. Typed image and video transfers may overlap.
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

    /**
     * Uploads a local image without a safety check.
     *
     * @param[in] path The UTF-8 path to the encoded file.
     * @param[in] content_type Optional MIME type; empty uses the file extension.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     * @see xmax_storage_upload_media
     */
    uploaded_file upload_image(
        const std::string& path,
        const std::string& content_type = {},
        storage_transfer_callbacks callbacks = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            nullptr,
            content_type.empty() ? nullptr : content_type.c_str(),
            false
        };
        return upload_media(
            path.c_str(),
            nullptr,
            0,
            options,
            XMAX_STORAGE_MEDIA_IMAGE,
            callbacks
        );
    }

    /**
     * Uploads encoded image bytes without a safety check.
     *
     * @param[in] data The encoded bytes borrowed for the duration of the call.
     * @param[in] size The accessible byte count.
     * @param[in] filename The UTF-8 object filename and MIME type hint.
     * @param[in] content_type Optional MIME type; empty uses the filename extension.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     * @see xmax_storage_upload_media
     */
    uploaded_file upload_image(
        const uint8_t* data,
        size_t size,
        const std::string& filename,
        const std::string& content_type = {},
        storage_transfer_callbacks callbacks = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            filename.c_str(),
            content_type.empty() ? nullptr : content_type.c_str(),
            false
        };
        return upload_media(
            nullptr,
            data,
            size,
            options,
            XMAX_STORAGE_MEDIA_IMAGE,
            callbacks
        );
    }

    /**
     * Uploads a local image with a service safety check.
     *
     * @param[in] path The UTF-8 path to the encoded file.
     * @param[in] content_type Optional MIME type; empty uses the file extension.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     * @see xmax_storage_upload_media
     */
    uploaded_file upload_image_with_safety_check(
        const std::string& path,
        const std::string& content_type = {},
        storage_transfer_callbacks callbacks = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            nullptr,
            content_type.empty() ? nullptr : content_type.c_str(),
            true
        };
        return upload_media(
            path.c_str(),
            nullptr,
            0,
            options,
            XMAX_STORAGE_MEDIA_IMAGE,
            callbacks
        );
    }

    /**
     * Uploads encoded image bytes with a service safety check.
     *
     * @param[in] data The encoded bytes borrowed for the duration of the call.
     * @param[in] size The accessible byte count.
     * @param[in] filename The UTF-8 object filename and MIME type hint.
     * @param[in] content_type Optional MIME type; empty uses the filename extension.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     * @see xmax_storage_upload_media
     */
    uploaded_file upload_image_with_safety_check(
        const uint8_t* data,
        size_t size,
        const std::string& filename,
        const std::string& content_type = {},
        storage_transfer_callbacks callbacks = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            filename.c_str(),
            content_type.empty() ? nullptr : content_type.c_str(),
            true
        };
        return upload_media(
            nullptr,
            data,
            size,
            options,
            XMAX_STORAGE_MEDIA_IMAGE,
            callbacks
        );
    }

    /**
     * Uploads a local encoded video.
     *
     * @param[in] path The UTF-8 path to the encoded file.
     * @param[in] content_type Optional MIME type; empty uses the file extension.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     * @see xmax_storage_upload_media
     */
    uploaded_file upload_video(
        const std::string& path,
        const std::string& content_type = {},
        storage_transfer_callbacks callbacks = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            nullptr,
            content_type.empty() ? nullptr : content_type.c_str(),
            false
        };
        return upload_media(
            path.c_str(),
            nullptr,
            0,
            options,
            XMAX_STORAGE_MEDIA_VIDEO,
            callbacks
        );
    }

    /**
     * Uploads encoded video bytes.
     *
     * @param[in] data The encoded bytes borrowed for the duration of the call.
     * @param[in] size The accessible byte count.
     * @param[in] filename The UTF-8 object filename and MIME type hint.
     * @param[in] content_type Optional MIME type; empty uses the filename extension.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     * @see xmax_storage_upload_media
     */
    uploaded_file upload_video(
        const uint8_t* data,
        size_t size,
        const std::string& filename,
        const std::string& content_type = {},
        storage_transfer_callbacks callbacks = {}
    ) {
        const xmax_upload_options options{
            sizeof(options),
            filename.c_str(),
            content_type.empty() ? nullptr : content_type.c_str(),
            false
        };
        return upload_media(
            nullptr,
            data,
            size,
            options,
            XMAX_STORAGE_MEDIA_VIDEO,
            callbacks
        );
    }

    /**
     * Downloads a remote image and publishes it to the destination after the transfer succeeds.
     *
     * @param[in] url The absolute HTTP or HTTPS source URL.
     * @param[in] destination The UTF-8 destination path. An existing file is replaced on success.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return The destination path and completed byte count.
     * @see xmax_storage_download_with_progress
     */
    downloaded_file download_image(
        const std::string& url,
        const std::string& destination,
        storage_transfer_callbacks callbacks = {}
    ) {
        return download_media(
            url,
            destination,
            callbacks
        );
    }

    /**
     * Downloads a remote video and publishes it to the destination after the transfer succeeds.
     *
     * @param[in] url The absolute HTTP or HTTPS source URL.
     * @param[in] destination The UTF-8 destination path. An existing file is replaced on success.
     * @param[in] callbacks Optional progress and cancellation callbacks for this transfer.
     * @return The destination path and completed byte count.
     * @see xmax_storage_download_with_progress
     */
    downloaded_file download_video(
        const std::string& url,
        const std::string& destination,
        storage_transfer_callbacks callbacks = {}
    ) {
        return download_media(
            url,
            destination,
            callbacks
        );
    }

  private:

    /**
     * Forwards a typed upload with the operation's byte source and callbacks.
     *
     * @param[in] path The UTF-8 file path, or null for encoded bytes.
     * @param[in] data The borrowed encoded bytes, or null for a file path.
     * @param[in] size The encoded byte count, or zero for a file path.
     * @param[in] options The sized upload metadata and safety policy.
     * @param[in] kind The expected media category.
     * @param[in] callbacks The callbacks for this transfer.
     * @return Owned metadata for the uploaded object.
     */
    uploaded_file upload_media(
        const char* path,
        const uint8_t* data,
        size_t size,
        const xmax_upload_options& options,
        int32_t kind,
        storage_transfer_callbacks callbacks
    ) {
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_media(
            get(),
            path,
            data,
            size,
            &options,
            kind,
            &result,
            callbacks.progress,
            callbacks.cancel,
            callbacks.user_data
        ));
        return uploaded_file(result);
    }

    /**
     * Downloads an object with atomic replacement and per-transfer callbacks.
     *
     * @param[in] url The absolute HTTPS source URL.
     * @param[in] destination The UTF-8 destination path.
     * @param[in] callbacks The callbacks for this transfer.
     * @return The destination path and completed byte count.
     */
    downloaded_file download_media(
        const std::string& url,
        const std::string& destination,
        storage_transfer_callbacks callbacks
    ) {
        downloaded_file result{destination, 0};
        check(xmax_storage_download_with_progress(
            get(),
            url.c_str(),
            destination.c_str(),
            &result.bytes,
            callbacks.progress,
            callbacks.cancel,
            callbacks.user_data
        ));
        return result;
    }
};
} // namespace xmax
