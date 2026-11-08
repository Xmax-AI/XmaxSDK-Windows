//
//  xmax_c_api.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "CAPI/ClientHandle.hpp"
#include "CAPI/CallbackCancellation.hpp"
#include "CAPI/RealtimeHandle.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Service/Media/MediaService.hpp"
#include "Foundation/Media/Audio/MicrophoneDevices.hpp"
#include "Foundation/Text/Utf8.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
using namespace xmax::detail;

/**
 * Releases an adopted listener context only after all captured callback references have been released.
 */
struct RemoteListenerContext {

    /**
     * The copied function pointers and context supplied by the integrating application.
     */
    xmax_remote_media_listener listener;

    /**
     * Whether registration succeeded and transferred context release responsibility to the SDK.
     */
    bool adopted = false;

    /**
     * Invokes the optional application context releaser after successful ownership transfer.
     */
    ~RemoteListenerContext() {
        if (adopted && listener.release_user_data) {
            try {
                listener.release_user_data(listener.user_data);
            } catch (...) {
                // Foreign context cleanup must not propagate exceptions through SDK destruction.
            }
        }
    }
};

/**
 * Owns a camera enumeration snapshot whose names and identifiers remain valid until release.
 */
struct xmax_camera_list_impl {

    /**
     * The camera information owned by this enumeration snapshot.
     */
    std::vector<CameraDevice> devices;
};

/**
 * Owns a storage transfer controller; released by xmax_storage_destroy.
 */
struct xmax_storage_impl {

    /**
     * The storage controller exclusively owned by this C handle.
     */
    std::unique_ptr<XmaxStorageManager> storage;
};

/**
 * Owns uploaded-object metadata; released by xmax_uploaded_file_release.
 */
struct xmax_uploaded_file_impl {

    /**
     * The object metadata owned by this upload-result handle.
     */
    StoredFile file;
};

/**
 * Retains frame ownership so pixels remain valid until handle release.
 */
struct xmax_frame_impl {

    /**
     * The shared frame reference that keeps pixels alive until handle release.
     */
    std::shared_ptr<Frame> frame;
};

/**
 * Owns NUL-terminated UTF-8 text; released by xmax_string_release.
 */
struct xmax_string_impl {

    /**
     * The UTF-8 text owned by this string handle.
     */
    std::string value;
};

namespace {

/**
 * The calling thread's NUL-terminated diagnostic buffer for the latest SDK operation.
 */
thread_local char last_error[1024]{};

/**
 * The calling thread's latest structured failure diagnostic.
 */
thread_local xmax_error_info last_error_detail{};

/**
 * Stores a redacted error message without truncating a UTF-8 character.
 *
 * @param[in] message The NUL-terminated diagnostic text to redact and truncate into thread-local storage.
 */
void set_error(
    const char* message
) noexcept {
    try {
        const auto safe = XmaxLogger::redact(message);
        auto length = std::min(safe.size(), sizeof(last_error) - 1);

        while (length && length < safe.size() && (static_cast<unsigned char>(safe[length]) & 0xc0) == 0x80)
            --length;

        std::memcpy(
            last_error,
            safe.data(),
            length
        );
        last_error[length] = 0;
    } catch (...) {
        std::strcpy(
            last_error,
            "SDK operation failed; diagnostic formatting unavailable"
        );
    }
}

/**
 * Logs failed C interface operations without recursively diagnosing log polling.
 *
 * @param[in] operation The failed C function's name, used for error logging and polling recursion
 * protection.
 * @param[in] code The negative SDK result returned by this C function.
 */
void log_failure(
    const char* operation,
    xmax_result code
) noexcept {
    if (std::strncmp(
            operation,
            "xmax_logger_",
            12
        ) == 0)
        return;

    XmaxLogger::write(
        LogCategory::API,
        code == XMAX_CANCELLED ? XMAX_LOG_WARNING : XMAX_LOG_ERROR,
        [&](auto language) {
            return std::string(operation) + "\n├─ " +
                   XmaxLogger::localized(
                       language,
                       "错误码：",
                       "Error Code: "
                   ) +
                   std::to_string(code) +
                   "\n└─ " + XmaxLogger::localized(language, "原因：", "Reason: ") + last_error;
        }
    );
}

/**
 * Catches exceptions at the C ABI boundary and produces result codes and thread-local diagnostics.
 *
 * @tparam F The callable type for the operation.
 *
 * @param[in] operation The C function name used for diagnostics and logging classification.
 * @param[in] f The operation to execute inside the C ABI exception boundary.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
template <class F>
xmax_result protect(
    const char* operation,
    F&& f
) noexcept {
    xmax_result code = XMAX_INTERNAL_ERROR;

    try {
        last_error[0] = 0;
        last_error_detail = {};
        last_error_detail.struct_size = sizeof(last_error_detail);
        f();
        return XMAX_OK;
    } catch (const Error& e) {
        set_error(e.what());
        code = e.code;
        last_error_detail.api_code = e.api_code;
        last_error_detail.http_status = e.http_status;
    } catch (const json::exception&) {
        set_error("Invalid JSON or UTF-8 data");
        code = XMAX_INVALID_ARGUMENT;
    } catch (const std::exception&) {
        set_error("Internal SDK operation failed");
    } catch (...) {
        set_error("Unknown SDK failure");
    }

    last_error_detail.code = code;
    std::memcpy(
        last_error_detail.message,
        last_error,
        sizeof(last_error)
    );
    log_failure(
        operation,
        code
    );

    return code;
}

/**
 * Validates an ABI structure's pointer and struct_size before access.
 *
 * @tparam T The input type used by the operation.
 *
 * @param[in] value The ABI structure pointer; struct_size must be at least the current structure's size.
 */
template <class T>
void sized(
    const T* value
) {
    require(
        value && value->struct_size >= sizeof(T),
        XMAX_INVALID_ARGUMENT,
        "Missing or incompatible structure size"
    );
}

/**
 * Rejects a null required handle or output pointer.
 *
 * @param[in] p The required non-null handle or output pointer.
 */
void handle(
    const void* p
) {
    require(
        p != nullptr,
        XMAX_INVALID_ARGUMENT,
        "Handle or output pointer is null"
    );
}

} // namespace

extern "C" {

/**
 * Returns the model's supported formats, pixel limits, and defaults.
 *
 * @see xmax_model_get_info in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_model_get_info(
    const char* model,
    xmax_model_info* info
) {
    return protect(
        __func__,
        [&] {
            sized(info);
            *info = RealtimeModelDefinition::info(RealtimeModelDefinition::parse(model ? model : "x2.1"));
        }
    );
}

/**
 * Copies the global configuration and creates a client without starting capture or a cloud session.
 *
 * @see xmax_client_create_with_options in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_client_create_with_options(
    const xmax_client_options* options,
    xmax_client* out_client
) {
    if (out_client)
        *out_client = nullptr;
    return protect(
        __func__,
        [&] {
            sized(options);
            handle(out_client);
            require(
                options->abi_version == XMAX_ABI_VERSION,
                XMAX_INVALID_ARGUMENT,
                "Unsupported C ABI version"
            );
            Config config;
            config.key = trim_utf8_whitespace(options->api_key ? options->api_key : "");
            config.model = "x2.1";
            config.environment = options->environment;
            config.timeout_ms = options->http_timeout_ms ? options->http_timeout_ms : 15000;
            auto client = std::make_unique<xmax_client_impl>(std::move(config));
            XmaxLogger::configure(
                options->logger_options,
                options->environment == XMAX_ENVIRONMENT_CHINA ? XMAX_LOG_CHINESE : XMAX_LOG_ENGLISH
            );
            *out_client = client.release();
        }
    );
}

/**
 * Creates a realtime controller with its own model configuration.
 *
 * @see xmax_realtime_create_with_config in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_create_with_config(
    xmax_client client,
    const xmax_realtime_config* config,
    xmax_realtime* out_realtime
) {
    if (out_realtime)
        *out_realtime = nullptr;
    return protect(
        __func__,
        [&] {
            handle(client);
            sized(config);
            handle(out_realtime);
            const std::string model = config->model ? config->model : "x2.1";
            RealtimeModelDefinition::parse(model);
            *out_realtime = new xmax_realtime_impl(client->client.create_realtime(model));
        }
    );
}

/**
 * Creates a realtime controller with an initial quality-adaptation policy.
 *
 * @param[in] client A valid client handle.
 * @param[in] options The creation options, copied during this call.
 * @param[out] out_realtime Receives the controller; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
xmax_result XMAX_CALL xmax_realtime_create_with_options(
    xmax_client client,
    const xmax_realtime_options* options,
    xmax_realtime* out_realtime
) {
    if (out_realtime)
        *out_realtime = nullptr;
    return protect(
        __func__,
        [&] {
            handle(client);
            sized(options);
            handle(out_realtime);
            require(
                options->adaptive_quality_enabled == 0 || options->adaptive_quality_enabled == 1,
                XMAX_INVALID_ARGUMENT,
                "Adaptive quality must be 0 or 1"
            );
            const std::string model = options->model ? options->model : "x2.1";
            RealtimeModelDefinition::parse(model);
            *out_realtime = new xmax_realtime_impl(
                client->client.create_realtime(
                    model,
                    options->adaptive_quality_enabled != 0
                )
            );
        }
    );
}

/**
 * Returns the calling thread's most recent structured error without clearing it.
 *
 * @see xmax_last_error_info in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_last_error_info(
    xmax_error_info* info
) {
    if (!info || info->struct_size < sizeof(*info))
        return XMAX_INVALID_ARGUMENT;
    *info = last_error_detail;
    info->struct_size = sizeof(*info);
    return XMAX_OK;
}

/**
 * Returns the current state, state reason, and associated error.
 *
 * @see xmax_realtime_get_state_info in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_get_state_info(
    xmax_realtime realtime,
    xmax_event_info* info
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(info);
            *info = realtime->session->state_info();
        }
    );
}

/**
 * Consumes the oldest event with its state reason and error details without blocking.
 *
 * @see xmax_realtime_poll_event_info in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_poll_event_info(
    xmax_realtime realtime,
    xmax_event_info* info
) {
    bool found = false;
    const auto result = protect(
        __func__,
        [&] {
            handle(realtime);
            sized(info);
            found = realtime->session->poll(*info);
        }
    );
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * Disconnects for the specified reason while retaining local media preview.
 *
 * @see xmax_realtime_disconnect_with_reason in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_disconnect_with_reason(
    xmax_realtime realtime,
    xmax_realtime_reason reason
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            realtime->session->disconnect_reason(reason);
        }
    );
}

/** @see xmax_realtime_disconnect_with_error in xmax.h for the public contract. */
xmax_result XMAX_CALL xmax_realtime_disconnect_with_error(
    xmax_realtime realtime,
    xmax_result code,
    const char* message,
    int32_t api_code,
    int32_t http_status
) {
    return protect(__func__, [&] {
        handle(realtime);
        require(code < 0 && message, XMAX_INVALID_ARGUMENT, "A negative error code and error message are required");
        realtime->session->disconnect_reason(XMAX_REASON_FAILURE, Error(code, message, api_code, http_status));
    });
}

/**
 * Updates a ready, disconnected camera's encoding format while retaining preview and device selection.
 *
 * @see xmax_realtime_update_camera_format in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_update_camera_format(
    xmax_realtime realtime,
    const xmax_video_format* format
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            handle(format);
            realtime->session->update_camera_format(*format);
        }
    );
}

/**
 * Changes a ready camera's resolution while retaining its frame rate and encoding configuration.
 *
 * @see xmax_realtime_update_camera_preset in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_update_camera_preset(
    xmax_realtime realtime,
    xmax_camera_preset preset
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            const auto current = realtime->session->camera_format();
            require(
                current.width != 0,
                XMAX_INVALID_STATE,
                "No camera stream is active"
            );
            realtime->session->update_camera_format(RealtimeModelDefinition::preset_format(preset, current.fps));
        }
    );
}

/**
 * Returns the camera's encoding format and preset, independent of network quality changes.
 *
 * @see xmax_realtime_get_camera_format in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_get_camera_format(
    xmax_realtime realtime,
    xmax_video_format* format,
    xmax_camera_preset* preset
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            handle(format);
            handle(preset);
            *format = realtime->session->camera_format();
            *preset = RealtimeModelDefinition::preset(*format);
        }
    );
}

/**
 * Sets the uplink level during generation without disabling subsequent network adaptation.
 *
 * @see xmax_realtime_set_uplink_quality_level in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_set_uplink_quality_level(
    xmax_realtime realtime,
    int32_t level
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            realtime->session->quality_level(
                0,
                level
            );
        }
    );
}

/**
 * Sets the downlink level during generation without disabling subsequent network adaptation.
 *
 * @see xmax_realtime_set_downlink_quality_level in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_set_downlink_quality_level(
    xmax_realtime realtime,
    int32_t level
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            realtime->session->quality_level(
                1,
                level
            );
        }
    );
}

/**
 * Maps trajectory samples to video coordinates and asynchronously sends tracks for the current task.
 *
 * @see xmax_realtime_submit_interaction in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_submit_interaction(
    xmax_realtime realtime,
    const xmax_interaction_frame* frame
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(frame);
            realtime->session->submit_interaction(*frame);
        }
    );
}

xmax_result XMAX_CALL xmax_realtime_submit_interaction_view(
    xmax_realtime realtime,
    const xmax_interaction_frame* frame,
    int32_t mirrored
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(frame);
            require(mirrored == 0 || mirrored == 1, XMAX_INVALID_ARGUMENT, "Invalid viewport mirror flag");
            realtime->session->submit_interaction(*frame, mirrored != 0);
        }
    );
}

/**
 * Submits a sampled gesture to a revision-scoped result stream.
 *
 * @see xmax_realtime_submit_stream_interaction in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_realtime_submit_stream_interaction(
    xmax_realtime realtime,
    uint64_t remote_revision,
    const xmax_interaction_frame* frame,
    int32_t mirrored
) {
    return protect(__func__, [&] {
        handle(realtime);
        sized(frame);
        require(remote_revision != 0, XMAX_INVALID_ARGUMENT, "A result stream revision is required");
        require(mirrored == 0 || mirrored == 1, XMAX_INVALID_ARGUMENT, "Invalid viewport mirror flag");
        realtime->session->submit_interaction(*frame, mirrored != 0, remote_revision);
    });
}

/**
 * Copies unexpired normalized trajectory points for custom effect rendering.
 *
 * @see xmax_realtime_get_trajectory in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_get_trajectory(
    xmax_realtime realtime,
    xmax_point* points,
    size_t capacity,
    size_t* count
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            handle(count);
            const auto values = realtime->session->trajectory();
            *count = values.size();
            require(
                capacity >= values.size(),
                XMAX_BUFFER_TOO_SMALL,
                "Trajectory buffer is too small"
            );
            if (!values.empty()) {
                handle(points);
                std::copy(
                    values.begin(),
                    values.end(),
                    points
                );
            }
        }
    );
}

/**
 * Renders a result frame and the default trajectory effect in an HWND.
 *
 * @see xmax_realtime_render_window in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_render_window(
    xmax_realtime realtime,
    xmax_frame frame,
    void* window,
    int32_t content_mode
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            handle(frame);
            realtime->session->render_window(
                *frame->frame,
                window,
                content_mode
            );
        }
    );
}

/**
 * Returns the SDK's semantic version.
 *
 * @see xmax_version in xmax.h for the public contract.
 */
const char* XMAX_CALL xmax_version(
    void
) {
    return XMAX_VERSION;
}

/**
 * Returns the calling thread's most recent SDK diagnostic message.
 *
 * @see xmax_last_error in xmax.h for the public contract.
 */
const char* XMAX_CALL xmax_last_error(
    void
) {
    return last_error;
}

/**
 * Configures process-wide logging options and language shared by all clients.
 *
 * @see xmax_logger_configure in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_logger_configure(
    uint32_t options,
    xmax_log_language language
) {
    return protect(
        __func__,
        [&] { XmaxLogger::configure(
                  options,
                  language
              ); }
    );
}

/**
 * Returns the process-wide logging configuration.
 *
 * @see xmax_logger_get_config in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_logger_get_config(
    uint32_t* options,
    xmax_log_language* language
) {
    return protect(
        __func__,
        [&] {
            handle(options);
            handle(language);

            const auto config = XmaxLogger::configuration();
            *options = config.options;
            *language = config.language;
        }
    );
}

/**
 * Acquires the oldest process-wide log record without blocking.
 *
 * @see xmax_logger_poll in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_logger_poll(
    xmax_log_record* record
) {
    bool found = false;
    const auto result = protect(
        __func__,
        [&] {
            sized(record);
            found = XmaxLogger::poll(*record);
        }
    );
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * Copies the configuration and creates a client without starting a camera or cloud session.
 *
 * @param[in] c The client configuration, with struct_size and XMAX_ABI_VERSION initialized; empty
 * credentials permit local preparation.
 * @param[out] out A non-null destination for the client handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release the handle with xmax_client_destroy. Input strings are copied during creation.
 */
xmax_result XMAX_CALL xmax_client_create(
    const xmax_client_config* c,
    xmax_client* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            sized(c);
            handle(out);
            require(
                c->abi_version == XMAX_ABI_VERSION,
                XMAX_INVALID_ARGUMENT,
                "Unsupported C ABI version"
            );

            Config config;
            config.key = trim_utf8_whitespace(c->api_key ? c->api_key : "");
            config.model = c->model ? c->model : "x2.1";
            config.timeout_ms = c->http_timeout_ms ? c->http_timeout_ms : 15000;

            auto client = std::make_unique<xmax_client_impl>(std::move(config));
            *out = client.release();
        }
    );
}

/**
 * Releases the client handle and its owned resources.
 *
 * @see xmax_client_destroy in xmax.h for the public contract.
 */
void XMAX_CALL xmax_client_destroy(
    xmax_client client
) {
    delete client;
}

/**
 * Reports whether this SDK build includes an RTC backend.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out A non-null destination receiving 1 when available, or 0 otherwise.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
xmax_result XMAX_CALL xmax_client_has_rtc(
    xmax_client client,
    int32_t* out
) {
    return protect(
        __func__,
        [&] {
            handle(client);
            handle(out);
            *out = client->client.has_rtc() ? 1 : 0;
        }
    );
}

/**
 * Returns this build's capability flags and ABI version.
 *
 * @param[in] client A non-null client handle.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
xmax_result XMAX_CALL xmax_client_get_capabilities(
    xmax_client client,
    xmax_capabilities* out
) {
    return protect(
        __func__,
        [&] {
            handle(client);
            sized(out);
            *out = {
                sizeof(*out),
                XMAX_ABI_VERSION,
                client->client.capabilities()
            };
        }
    );
}

/**
 * Enumerates camera devices without starting capture or a cloud session.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out A non-null destination for the enumeration snapshot; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release the snapshot with xmax_camera_list_release.
 */
xmax_result XMAX_CALL xmax_client_list_cameras(
    xmax_client client,
    xmax_camera_list* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            handle(client);
            handle(out);
            *out = new xmax_camera_list_impl{client->client.cameras()};
        }
    );
}

/**
 * Returns the number of devices in a camera enumeration snapshot.
 *
 * @see xmax_camera_list_count in xmax.h for the public contract.
 */
size_t XMAX_CALL xmax_camera_list_count(
    xmax_camera_list list
) {
    return list ? list->devices.size() : 0;
}

/**
 * Returns information about a camera in the enumeration snapshot.
 *
 * @param[in] list A valid, non-null enumeration snapshot.
 * @param[in] index The zero-based device index, less than the device count.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Device strings belong to the snapshot and remain valid until it is released.
 */
xmax_result XMAX_CALL xmax_camera_list_get(
    xmax_camera_list list,
    size_t index,
    xmax_camera_info* out
) {
    return protect(
        __func__,
        [&] {
            handle(list);
            sized(out);
            require(
                index < list->devices.size(),
                XMAX_INVALID_ARGUMENT,
                "Camera index is out of range"
            );

            const auto& device = list->devices[index];
            *out = {
                sizeof(*out),
                device.id.c_str(),
                device.name.c_str(),
                device.is_default ? 1 : 0
            };
        }
    );
}

/**
 * Copies the enclosure position from a camera enumeration snapshot.
 *
 * @see xmax_camera_list_get_position
 */
xmax_result XMAX_CALL xmax_camera_list_get_position(
    xmax_camera_list list,
    size_t index,
    xmax_camera_position* position
) {
    return protect(
        __func__,
        [&] {
            handle(list);
            handle(position);
            require(
                index < list->devices.size(),
                XMAX_INVALID_ARGUMENT,
                "Camera index is out of range"
            );
            *position = list->devices[index].position;
        }
    );
}

/**
 * Releases a camera enumeration snapshot and its device strings.
 *
 * @see xmax_camera_list_release in xmax.h for the public contract.
 */
void XMAX_CALL xmax_camera_list_release(
    xmax_camera_list list
) {
    delete list;
}

/**
 * Creates an independent realtime controller without connecting to the cloud.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out A non-null destination for the realtime handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release with xmax_realtime_destroy. The handle independently retains the client's service
 * configuration.
 */
xmax_result XMAX_CALL xmax_realtime_create(
    xmax_client client,
    xmax_realtime* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            handle(client);
            handle(out);
            *out = new xmax_realtime_impl(client->client.create_realtime());
        }
    );
}

/**
 * Closes and releases the realtime controller's session, RTC, and media resources.
 *
 * @see xmax_realtime_destroy in xmax.h for the public contract.
 */
void XMAX_CALL xmax_realtime_destroy(
    xmax_realtime realtime
) {
    if (realtime)
        realtime->session->shutdown();
    delete realtime;
}

/**
 * Binds an external input supplied continuously by your app.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] f The input dimensions and frame rate; see xmax_video_format for limits.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Bind before connecting; binding stops the previous local
 * media input.
 */
xmax_result XMAX_CALL xmax_realtime_prepare_external(
    xmax_realtime r,
    const xmax_video_format* f
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            handle(f);
            r->session->prepare(*f);
        }
    );
}

static xmax_result create_local_camera_stream(
    xmax_realtime r,
    const xmax_local_camera_options* options,
    bool wait_for_preview,
    const xmax_encoding_config* encoding = nullptr
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(options);
            require(
                options->mirror == 0 || options->mirror == 1,
                XMAX_INVALID_ARGUMENT,
                "Mirror must be zero or one"
            );
            require(
                options->microphone >= XMAX_MICROPHONE_DISABLED &&
                    options->microphone <= XMAX_MICROPHONE_DEVICE,
                XMAX_INVALID_ARGUMENT,
                "Invalid microphone selection"
            );
            if (encoding)
                sized(encoding);
            const std::string microphone = options->microphone_device_id ? options->microphone_device_id : "";
            require(
                options->microphone == XMAX_MICROPHONE_DEVICE ? !microphone.empty() : microphone.empty(),
                XMAX_INVALID_ARGUMENT,
                "Microphone endpoint must match its selection kind"
            );
            auto format = options->video_format;
            if (!format.width && !format.height && !format.fps)
                format = r->session->model_info().default_camera_format;
            r->session->create_local_camera_stream(
                options->device_id ? options->device_id : "",
                format,
                options->microphone != XMAX_MICROPHONE_DISABLED,
                options->mirror != 0,
                microphone,
                wait_for_preview,
                encoding ? std::optional{*encoding} : std::nullopt
            );
        }
    );
}

xmax_result XMAX_CALL xmax_realtime_create_local_camera_stream(
    xmax_realtime realtime,
    const xmax_local_camera_options* options
) {
    return create_local_camera_stream(realtime, options, false);
}

xmax_result XMAX_CALL xmax_realtime_create_local_camera_preview_stream(
    xmax_realtime realtime,
    const xmax_local_camera_options* options
) {
    return create_local_camera_stream(realtime, options, true);
}

/**
 * Binds a camera and its encoder overrides.
 *
 * @see xmax_realtime_create_local_camera_stream_with_encoding in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_realtime_create_local_camera_stream_with_encoding(
    xmax_realtime realtime,
    const xmax_local_camera_options* options,
    const xmax_encoding_config* encoding
) {
    return create_local_camera_stream(realtime, options, true, encoding);
}

/**
 * Updates a camera binding and its encoder overrides.
 *
 * @see xmax_realtime_update_camera_format_with_encoding in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_realtime_update_camera_format_with_encoding(
    xmax_realtime realtime,
    const xmax_video_format* format,
    const xmax_encoding_config* encoding
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(format);
        sized(encoding);
        realtime->session->update_camera_format(*format, *encoding);
    });
}

/**
 * Copies the controller encoder configuration.
 *
 * @see xmax_realtime_get_encoding in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_realtime_get_encoding(
    xmax_realtime realtime,
    xmax_encoding_config* encoding
) {
    return protect(__func__, [&] {
        handle(realtime);
        sized(encoding);
        *encoding = realtime->session->encoding_config();
        encoding->struct_size = sizeof(*encoding);
    });
}

xmax_result XMAX_CALL xmax_realtime_complete_camera_preview(
    xmax_realtime realtime,
    uint64_t revision
) {
    return protect(__func__, [&] {
        handle(realtime);
        realtime->session->complete_camera_preview(revision);
    });
}

xmax_result XMAX_CALL xmax_realtime_list_cameras(
    xmax_realtime realtime,
    xmax_camera_list* out
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        handle(realtime);
        handle(out);
        *out = new xmax_camera_list_impl{make_camera_capture()->devices()};
    });
}

/**
 * Switches the active camera while retaining the connection and restoring generation conditions.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] device A non-null UTF-8 device identifier; empty selects the default camera.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @see xmax_realtime_switch_camera in xmax.h for the recovery and connection contract.
 */
xmax_result XMAX_CALL xmax_realtime_switch_camera(
    xmax_realtime r,
    const char* device
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            handle(device);
            r->session->switch_camera(device);
        }
    );
}

/**
 * Releases local capture and input bindings after disconnection.
 *
 * @param[in] r A non-null realtime controller handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Disconnect any active connection first.
 */
xmax_result XMAX_CALL xmax_realtime_stop_media(
    xmax_realtime r
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            r->session->stop_media();
        }
    );
}

/**
 * Creates a server session and joins RTC without starting generation.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] microphone A nonzero value enables the microphone when joining the room.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Prepare local media first. The camera configuration's
 * microphone option also applies.
 */
xmax_result XMAX_CALL xmax_realtime_connect(
    xmax_realtime r,
    int32_t microphone
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            r->session->connect(microphone != 0);
        }
    );
}

/**
 * Starts generation with supplied or cached conditions and waits for the first result frame.
 *
 * A newer generation request cancels an unfinished one with XMAX_CANCELLED. The newer request executes
 * after the previous request's cleanup; only successful requests replace cached generation conditions.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] context Generation conditions whose strings remain valid until this call returns. NULL reuses
 * cached conditions; the first generation requires conditions.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Ready media connects first if needed; supply external frames
 * continuously from another thread.
 */
xmax_result XMAX_CALL xmax_realtime_generate(
    xmax_realtime r,
    const xmax_generation_context* context
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            if (context) {
                sized(context);
                handle(context->prompt);
            }
            r->session->generate(context);
        }
    );
}

/**
 * Returns a state and session-identifier snapshot without consuming queued events.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This query may wait for the serialized lifecycle queue.
 */
xmax_result XMAX_CALL xmax_realtime_get_state(
    xmax_realtime r,
    xmax_event* out
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(out);
            *out = r->session->state_snapshot();
        }
    );
}

/**
 * Returns the external input format or camera RTC encoding target; local frame views report actual capture
 * dimensions.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[out] out A non-null destination for the input format.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This query may wait for the serialized lifecycle queue.
 */
xmax_result XMAX_CALL xmax_realtime_get_format(
    xmax_realtime r,
    xmax_video_format* out
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            handle(out);
            *out = r->session->format();
        }
    );
}

/**
 * Returns the remote playback volume for the current or next connection.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[out] out A non-null destination for the volume in [0, 1].
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The initial volume is 0. This query may wait for the serialized lifecycle queue.
 */
xmax_result XMAX_CALL xmax_realtime_get_remote_volume(
    xmax_realtime r,
    float* out
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            handle(out);
            *out = r->session->volume();
        }
    );
}

/**
 * Configures uplink bitrate limits and encoding degradation preference.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] config The encoding configuration with struct_size initialized; -1 selects the minimum-bitrate
 * policy default, and 0 selects the estimated maximum.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. An active connection updates its native encoder.
 */
xmax_result XMAX_CALL xmax_realtime_set_encoding(
    xmax_realtime r,
    const xmax_encoding_config* config
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(config);
            r->session->encoding(*config);
        }
    );
}

/**
 * Configures local preview, remote display, and published-video mirror settings.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] config A non-null mirror configuration with struct_size initialized.
 *
 * @return XMAX_OK on success; a negative error code on failure, preserving the previous configuration.
 */
xmax_result XMAX_CALL xmax_realtime_set_video_mirror(
    xmax_realtime realtime,
    const xmax_video_mirror_config* config
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(config);
            realtime->session->video_mirror(*config);
        }
    );
}

/**
 * Returns the session's retained mirror configuration.
 *
 * @see xmax_realtime_get_video_mirror in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_get_video_mirror(
    xmax_realtime realtime,
    xmax_video_mirror_config* config
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(config);
            *config = realtime->session->video_mirror();
        }
    );
}

/**
 * Returns the horizontal display-mirror flag captured at acquisition without changing pixels.
 *
 * @param[in] frame A valid, non-null frame handle.
 * @param[out] mirrored A non-null destination for the 0 or 1 mirror flag.
 *
 * @return XMAX_OK on success; a negative error code on failure, with the output set to 0.
 */
xmax_result XMAX_CALL xmax_frame_get_mirrored(
    xmax_frame frame,
    int32_t* mirrored
) {
    if (mirrored)
        *mirrored = 0;
    return protect(
        __func__,
        [&] {
            handle(frame);
            handle(mirrored);
            *mirrored = frame->frame->presentation_mirrored ? 1 : 0;
        }
    );
}

/**
 * Enables or disables independent uplink and downlink quality adaptation.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] enabled Nonzero enables adaptation; 0 disables it. Changes reset the network observation
 * window.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
xmax_result XMAX_CALL xmax_realtime_set_adaptive_quality(
    xmax_realtime r,
    int32_t enabled
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            r->session->adaptive(enabled != 0);
        }
    );
}

/**
 * Returns directional quality levels and effective video formats.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This query may wait for the serialized lifecycle queue.
 */
xmax_result XMAX_CALL xmax_realtime_get_quality(
    xmax_realtime r,
    xmax_quality_status* out
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(out);
            *out = r->session->quality();
        }
    );
}

/**
 * Connects and starts generation with initial conditions, waiting for the first result frame.
 *
 * A newer generation request cancels an unfinished one with XMAX_CANCELLED. The newer request executes
 * after the previous request's cleanup; only successful requests replace cached generation conditions.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] c Non-null generation conditions whose strings remain valid until this synchronous call
 * returns.
 * @param[in] microphone A nonzero value enables the microphone when connecting.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Prepare media first and submit external frames continuously
 * from another thread.
 */
xmax_result XMAX_CALL xmax_realtime_start(
    xmax_realtime r,
    const xmax_generation_context* c,
    int32_t microphone
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(c);
            handle(c->prompt);
            r->session->start(
                c->prompt,
                c->reference_path ? c->reference_path : "",
                c->face_restoration != 0,
                microphone != 0
            );
        }
    );
}

/**
 * Replaces the running generation task's conditions.
 *
 * A newer generation request cancels an unfinished one with XMAX_CANCELLED. The newer request executes
 * after the previous request's cleanup; only successful requests replace cached generation conditions.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] c Non-null generation conditions whose strings remain valid until this synchronous call
 * returns.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Generation must already be active.
 */
xmax_result XMAX_CALL xmax_realtime_update(
    xmax_realtime r,
    const xmax_generation_context* c
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(c);
            handle(c->prompt);
            r->session->update(
                c->prompt,
                c->reference_path ? c->reference_path : "",
                c->face_restoration != 0
            );
        }
    );
}

/**
 * Disconnects the server session and RTC, clears generation conditions, and retains local preview.
 *
 * @param[in] r A non-null realtime controller handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. It also requests cancellation of in-flight lifecycle
 * operations.
 */
xmax_result XMAX_CALL xmax_realtime_disconnect(
    xmax_realtime r
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            r->session->disconnect(false);
        }
    );
}

/**
 * Disconnects and releases local media and cached generation conditions.
 *
 * @param[in] r A non-null realtime controller handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. The handle remains valid for preparing media and starting
 * another session.
 */
xmax_result XMAX_CALL xmax_realtime_close(
    xmax_realtime r
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            r->session->disconnect(true);
        }
    );
}

/**
 * Reports whether your app should currently supply external video frames.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[out] out A non-null destination for the request flag; camera input always returns 0.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The flag stays true while a live external source drives disconnected preview. It becomes false
 * after unbinding or source failure. This flag does not pace frames or wait for lifecycle work.
 */
xmax_result XMAX_CALL xmax_realtime_is_requesting_frames(
    xmax_realtime r,
    int32_t* out
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            handle(out);
            *out = r->session->requesting() ? 1 : 0;
        }
    );
}

/**
 * Copies and submits an external video frame synchronously.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] f A valid video view with struct_size initialized, matching bound dimensions and a strictly
 * increasing timestamp.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The source pixels can be reused after return. External frames are rejected with SDK camera input;
 * disconnected external input still supports local preview.
 */
xmax_result XMAX_CALL xmax_realtime_push_video_frame(
    xmax_realtime r,
    const xmax_video_frame* f
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            sized(f);
            r->session->push(*f);
        }
    );
}

/**
 * Sets the remote playback volume for the current or next connection.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in] v A finite value in [0, 1]; 0 mutes playback.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
xmax_result XMAX_CALL xmax_realtime_set_remote_volume(
    xmax_realtime r,
    float v
) {
    return protect(
        __func__,
        [&] {
            handle(r);
            r->session->volume(v);
        }
    );
}

/**
 * Consumes the oldest pending application event without blocking.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 */
xmax_result XMAX_CALL xmax_realtime_poll_event(
    xmax_realtime r,
    xmax_event* out
) {
    bool found = false;
    auto result = protect(
        __func__,
        [&] {
            handle(r);
            sized(out);
            found = r->session->poll(*out);
        }
    );
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * Acquires the latest remote result frame without blocking.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[out] out A non-null destination for the frame handle. Pixels remain valid until release; no frame
 * or failure clears the handle to NULL.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 *
 * @note Release with xmax_frame_release. New frames replace unconsumed frames.
 */
xmax_result XMAX_CALL xmax_realtime_acquire_frame(
    xmax_realtime r,
    xmax_frame* out
) {
    if (out)
        *out = nullptr;

    bool found = false;
    auto result = protect(
        __func__,
        [&] {
            handle(r);
            handle(out);

            auto frame = r->session->acquire_frame();

            if (frame) {
                *out = new xmax_frame_impl{std::move(frame)};
                found = true;
            }
        }
    );

    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

xmax_result XMAX_CALL xmax_realtime_get_local_stream_revision(
    xmax_realtime realtime,
    uint64_t* revision
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(revision);
        *revision = realtime->session->local_stream_revision();
    });
}

xmax_result XMAX_CALL xmax_realtime_connect_stream(
    xmax_realtime realtime,
    uint64_t local_revision,
    uint64_t* remote_revision
) {
    return xmax_realtime_connect_stream_cancellable(realtime, local_revision, remote_revision, nullptr, nullptr);
}

xmax_result XMAX_CALL xmax_realtime_connect_stream_cancellable(
    xmax_realtime realtime,
    uint64_t local_revision,
    uint64_t* remote_revision,
    xmax_cancellation_fn cancel,
    void* user_data
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(remote_revision);
        require(local_revision != 0, XMAX_INVALID_ARGUMENT, "A local stream revision is required");
        const CallbackCancellation cancellation(cancel, user_data);
        *remote_revision = realtime->session->connect(false, local_revision, cancellation.predicate());
    });
}

xmax_result XMAX_CALL xmax_realtime_start_generation_stream(
    xmax_realtime realtime,
    uint64_t local_revision,
    const xmax_generation_context* context,
    int32_t face_restoration,
    uint64_t* remote_revision
) {
    return xmax_realtime_start_generation_cancellable(
        realtime,
        local_revision,
        context,
        face_restoration,
        remote_revision,
        0,
        nullptr,
        nullptr
    );
}

/**
 * Runs generation with an operation-scoped cancellation callback.
 *
 * @see xmax_realtime_start_generation_cancellable in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_realtime_start_generation_cancellable(
    xmax_realtime realtime,
    uint64_t local_revision,
    const xmax_generation_context* context,
    int32_t face_restoration,
    uint64_t* remote_revision,
    int32_t update_only,
    xmax_cancellation_fn cancel,
    void* user_data
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(remote_revision);
        require(
            face_restoration >= -1 && face_restoration <= 1,
            XMAX_INVALID_ARGUMENT,
            "Invalid face restoration flag"
        );
        if (context) {
            sized(context);
            handle(context->prompt);
        }
        require(update_only == 0 || update_only == 1, XMAX_INVALID_ARGUMENT, "Invalid generation update flag");
        const CallbackCancellation cancellation(cancel, user_data);
        *remote_revision = realtime->session->generate(
            context,
            local_revision,
            face_restoration == -1 ? std::nullopt : std::optional<bool>(face_restoration != 0),
            local_revision != 0,
            cancellation.predicate(),
            update_only != 0
        );
    });
}

xmax_result XMAX_CALL xmax_realtime_acquire_stream_frame(
    xmax_realtime realtime,
    int32_t local,
    uint64_t revision,
    xmax_frame* out
) {
    if (out)
        *out = nullptr;
    const auto result = protect(__func__, [&] {
        handle(realtime);
        handle(out);
        require(revision != 0, XMAX_INVALID_ARGUMENT, "A stream revision is required");
        auto frame = local ? realtime->session->acquire_local_frame(revision)
                           : realtime->session->acquire_frame(revision);
        if (frame)
            *out = new xmax_frame_impl{std::move(frame)};
    });
    return result == XMAX_OK && !*out ? XMAX_NO_EVENT : result;
}

xmax_result XMAX_CALL xmax_realtime_acquire_result_video_frame(
    xmax_realtime realtime,
    uint64_t track_revision,
    uint64_t* content_revision,
    xmax_frame* out
) {
    if (out)
        *out = nullptr;
    const auto result = protect(__func__, [&] {
        handle(realtime);
        handle(content_revision);
        handle(out);
        require(track_revision != 0, XMAX_INVALID_ARGUMENT, "A result stream revision is required");
        auto frame = realtime->session->acquire_result(track_revision, *content_revision);
        if (frame)
            *out = new xmax_frame_impl{std::move(frame)};
    });
    return result == XMAX_OK && !*out ? XMAX_NO_EVENT : result;
}

xmax_result XMAX_CALL xmax_realtime_get_video_track_info(
    xmax_realtime realtime,
    int32_t local,
    uint64_t revision,
    xmax_video_format* format,
    xmax_encoding_config* encoding,
    xmax_camera_list* camera
) {
    if (camera)
        *camera = nullptr;
    if (format)
        *format = {};
    bool found = false;
    const auto result = protect(__func__, [&] {
        handle(realtime);
        handle(format);
        if (encoding)
            sized(encoding);
        require((local == 0 || local == 1) && revision != 0, XMAX_INVALID_ARGUMENT, "Invalid video track binding");
        std::optional<CameraDevice> device;
        found = realtime->session->read_video_track(revision, local != 0, *format, encoding, camera ? &device : nullptr);
        if (camera && device)
            *camera = new xmax_camera_list_impl{{std::move(*device)}};
    });
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * Acquires the latest local preview frame without blocking.
 *
 * @param[in] r A non-null realtime controller handle.
 * @param[out] out A non-null destination for the frame handle. Pixels remain valid until release; no frame
 * or failure clears the handle to NULL.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 *
 * @note Release with xmax_frame_release. Acquisition does not wait for capture.
 */
xmax_result XMAX_CALL xmax_realtime_acquire_local_frame(
    xmax_realtime r,
    xmax_frame* out
) {
    if (out)
        *out = nullptr;

    bool found = false;
    const auto result = protect(
        __func__,
        [&] {
            handle(r);
            handle(out);

            auto frame = r->session->acquire_local_frame();

            if (frame) {
                *out = new xmax_frame_impl{std::move(frame)};
                found = true;
            }
        }
    );

    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * Renders a video frame on the target HWND's owner thread.
 *
 * @param[in] frame A valid, non-null frame handle.
 * @param[in] hwnd The target Windows HWND cast to void*.
 * @param[in] mode XMAX_CONTENT_FIT for aspect fit, or XMAX_CONTENT_FILL for centered aspect fill.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Call on the HWND owner thread. Platforms without native window rendering return XMAX_UNSUPPORTED.
 */
xmax_result XMAX_CALL xmax_frame_render_window(
    xmax_frame frame,
    void* hwnd,
    int32_t mode
) {
    return protect(
        __func__,
        [&] {
            handle(frame);
            render_window(
                *frame->frame,
                hwnd,
                mode
            );
        }
    );
}

/**
 * Creates an independent storage transfer controller.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out A non-null destination for the storage handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release with xmax_storage_destroy. The handle independently retains the client configuration.
 */
xmax_result XMAX_CALL xmax_storage_create(
    xmax_client client,
    xmax_storage* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            handle(client);
            handle(out);
            *out = new xmax_storage_impl{client->client.create_storage()};
        }
    );
}

/**
 * Releases a storage transfer controller.
 *
 * @param[in] value The storage handle to release; may be NULL.
 *
 * @note Cancel and wait for in-flight transfers and finish concurrent handle use before release.
 */
void XMAX_CALL xmax_storage_destroy(
    xmax_storage value
) {
    delete value;
}

/**
 * Requests cancellation of the current storage transfer.
 *
 * @param[in] value The storage handle; may be NULL.
 *
 * @note May be called from another thread; does not wait for transfer completion.
 */
void XMAX_CALL xmax_storage_cancel(
    xmax_storage value
) {
    if (value)
        value->storage->cancel();
}

/**
 * Returns a thread-safe transfer state and byte-progress snapshot.
 *
 * @param[in] value A non-null storage handle. Each instance permits one active transfer.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Thread-safe. Total bytes are 0 when unknown.
 */
xmax_result XMAX_CALL xmax_storage_get_progress(
    xmax_storage value,
    xmax_transfer_progress* out
) {
    return protect(
        __func__,
        [&] {
            handle(value);
            sized(out);
            *out = value->storage->progress();
        }
    );
}

/**
 * Uploads an encoded local image or video file.
 *
 * @param[in] value A non-null storage handle. Each instance permits one active transfer.
 * @param[in] path A non-null UTF-8 local file path.
 * @param[in] options Optional upload settings; NULL infers filename and MIME type and disables image safety
 * checks.
 * @param[out] out A non-null destination for the upload result handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. File contents are streamed to storage.
 * Release results with xmax_uploaded_file_release.
 */
xmax_result XMAX_CALL xmax_storage_upload_file(
    xmax_storage value,
    const char* path,
    const xmax_upload_options* options,
    xmax_uploaded_file* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            handle(value);
            handle(path);
            handle(out);

            if (options)
                sized(options);

            const auto name = options && options->file_name ? options->file_name : "";
            const auto type = options && options->content_type ? options->content_type : "";
            auto file =
                value->storage->upload_file(
                    path,
                    name,
                    type,
                    options && options->check_image_safety != 0
                );
            *out = new xmax_uploaded_file_impl{std::move(file)};
        }
    );
}

/**
 * Uploads encoded file bytes, not raw video pixels.
 *
 * @param[in] value A non-null storage handle. Each instance permits one active transfer.
 * @param[in] data Non-null encoded file bytes, valid throughout this call.
 * @param[in] length The accessible byte count, greater than 0 and within the media-type limit.
 * @param[in] options The upload configuration with struct_size initialized and a filename provided.
 * @param[out] out A non-null destination for the upload result handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Keep input data valid until return; release results with
 * xmax_uploaded_file_release.
 */
xmax_result XMAX_CALL xmax_storage_upload_data(
    xmax_storage value,
    const uint8_t* data,
    size_t length,
    const xmax_upload_options* options,
    xmax_uploaded_file* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            handle(value);
            handle(data);
            sized(options);
            handle(options->file_name);
            handle(out);

            auto file = value->storage->upload_data(
                data,
                length,
                options->file_name,
                options->content_type ? options->content_type : "",
                options->check_image_safety != 0
            );
            *out = new xmax_uploaded_file_impl{std::move(file)};
        }
    );
}

/**
 * Downloads HTTPS content to a local file that does not yet exist.
 *
 * @param[in] value A non-null storage handle. Each instance permits one active transfer.
 * @param[in] url A non-null absolute HTTPS URL; redirects are not accepted.
 * @param[in] destination A non-null UTF-8 destination path; an existing file causes failure.
 * @param[out] bytes A non-null destination for the completed file's byte count.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Downloads are limited to 256 MiB; the destination becomes
 * visible only on success.
 */
xmax_result XMAX_CALL xmax_storage_download(
    xmax_storage value,
    const char* url,
    const char* destination,
    uint64_t* bytes
) {
    if (bytes)
        *bytes = 0;
    return protect(
        __func__,
        [&] {
            handle(value);
            handle(url);
            handle(destination);
            handle(bytes);
            *bytes = value->storage->download(
                url,
                destination
            );
        }
    );
}

/**
 * Returns borrowed metadata from an upload result.
 *
 * @param[in] file A non-null upload result handle.
 * @param[in,out] out A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Strings remain valid until the upload result is released; a missing ETag has a NULL pointer.
 */
xmax_result XMAX_CALL xmax_uploaded_file_get_info(
    xmax_uploaded_file file,
    xmax_uploaded_file_info* out
) {
    return protect(
        __func__,
        [&] {
            handle(file);
            sized(out);
            *out = {
                sizeof(*out),
                file->file.url.c_str(),
                file->file.object_key.c_str(),
                file->file.etag.empty() ? nullptr : file->file.etag.c_str()
            };
        }
    );
}

/**
 * Uploads a file or encoded bytes with a typed media policy.
 *
 * @see xmax_storage_upload_media in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_storage_upload_media(
    xmax_storage storage,
    const char* path,
    const uint8_t* data,
    size_t length,
    const xmax_upload_options* options,
    int32_t media_kind,
    xmax_uploaded_file* out,
    xmax_storage_progress_fn progress,
    xmax_cancellation_fn cancel,
    void* user_data
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        handle(storage);
        handle(out);
        sized(options);
        require((path && !data && length == 0) || (!path && data && length),
                XMAX_INVALID_ARGUMENT, "Provide one media input");
        require(media_kind == XMAX_STORAGE_MEDIA_IMAGE || media_kind == XMAX_STORAGE_MEDIA_VIDEO,
                XMAX_INVALID_ARGUMENT, "Invalid media type");
        const bool image = media_kind == XMAX_STORAGE_MEDIA_IMAGE;
        const std::string name = options->file_name ? options->file_name :
            path ? std::filesystem::u8path(path).filename().u8string() : "";
        auto type = options->content_type ? std::string(options->content_type) : std::string{};
        if (type.empty())
            type = StorageService::content_type(name);
        else
            type = trim_utf8_whitespace(type);
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        require(type.rfind(image ? "image/" : "video/", 0) == 0,
                XMAX_INVALID_ARGUMENT, image ? "Expected an image content type" : "Expected a video content type");
        const auto report = [progress, user_data](uint64_t bytes, uint64_t total) {
            if (progress) progress(user_data, bytes, total);
        };
        const auto cancelled = [cancel, user_data] { return cancel && cancel(user_data) != 0; };
        const bool safety = options->check_image_safety != 0;
        auto file = path ? storage->storage->upload_file(path, name, type, safety, report, cancelled, true)
                         : storage->storage->upload_data(data, length, name, type, safety, report, cancelled, true);
        *out = new xmax_uploaded_file_impl{std::move(file)};
    });
}

/**
 * Downloads to a staged file with transfer callbacks.
 *
 * @see xmax_storage_download_with_progress in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_storage_download_with_progress(
    xmax_storage storage,
    const char* url,
    const char* destination,
    uint64_t* bytes,
    xmax_storage_progress_fn progress,
    xmax_cancellation_fn cancel,
    void* user_data
) {
    if (bytes)
        *bytes = 0;
    return protect(__func__, [&] {
        handle(storage);
        handle(url);
        handle(destination);
        handle(bytes);
        *bytes = storage->storage->download(
            url, destination,
            [progress, user_data](uint64_t completed, uint64_t total) {
                if (progress) progress(user_data, completed, total);
            },
            [cancel, user_data] { return cancel && cancel(user_data) != 0; },
            true
        );
    });
}

/**
 * Releases an upload result and its metadata strings.
 *
 * @see xmax_uploaded_file_release in xmax.h for the public contract.
 */
void XMAX_CALL xmax_uploaded_file_release(
    xmax_uploaded_file file
) {
    delete file;
}

/**
 * Returns a borrowed view of the frame's pixel layout.
 *
 * @param[in] frame A valid, non-null frame handle.
 * @param[in,out] view A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The view does not retain pixels. Do not access it after releasing the frame handle.
 */
xmax_result XMAX_CALL xmax_frame_get_view(
    xmax_frame frame,
    xmax_video_frame* view
) {
    return protect(
        __func__,
        [&] {
            handle(frame);
            sized(view);
            *view = frame->frame->view();
        }
    );
}

/**
 * Releases the frame handle and its retained pixel storage.
 *
 * @see xmax_frame_release in xmax.h for the public contract.
 */
void XMAX_CALL xmax_frame_release(
    xmax_frame frame
) {
    delete frame;
}

/**
 * Performs an authenticated JSON request under the model's API root.
 *
 * @param[in] c A non-null client handle.
 * @param[in] method The HTTP method: GET, POST, PUT, or DELETE.
 * @param[in] path A relative API path; absolute URLs to other hosts are not permitted.
 * @param[in] body An optional UTF-8 JSON body; NULL omits the body.
 * @param[out] out A non-null destination for the response data payload as a JSON string handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Release the text with xmax_string_release; the full response
 * envelope is not returned.
 */
xmax_result XMAX_CALL xmax_client_request_json(
    xmax_client c,
    const char* method,
    const char* path,
    const char* body,
    xmax_string* out
) {
    if (out)
        *out = nullptr;
    return protect(
        __func__,
        [&] {
            handle(c);
            handle(method);
            handle(path);
            handle(out);

            json parsed;

            if (body)
                parsed = json::parse(body);

            auto data = c->client.request(
                method,
                path,
                body ? &parsed : nullptr,
                [] { return false; }
            );
            *out = new xmax_string_impl{data.dump()};
        }
    );
}

/**
 * Returns borrowed UTF-8 content from an SDK string.
 *
 * @param[in] value The SDK string handle; may be NULL.
 *
 * @return Borrowed NUL-terminated text, or NULL for an empty handle; do not modify or release it.
 *
 * @note The text remains valid until its string handle is released.
 */
const char* XMAX_CALL xmax_string_data(
    xmax_string value
) {
    return value ? value->value.c_str() : nullptr;
}

/**
 * Releases an SDK string and its text storage.
 *
 * @param[in] value The string handle to release; may be NULL.
 */
void XMAX_CALL xmax_string_release(
    xmax_string value
) {
    delete value;
}
}

/**
 * Sets serialized remote PCM and video delivery without changing playback volume or subscriptions.
 *
 * @see xmax_realtime_set_remote_media_frame_listener in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_set_remote_media_frame_listener(
    xmax_realtime realtime,
    const xmax_remote_media_listener* listener
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            if (!listener) {
                realtime->session->media_listener({});
                return;
            }
            sized(listener);
            require(
                listener->on_frame != nullptr,
                XMAX_INVALID_ARGUMENT,
                "A remote media callback is required"
            );

            auto context = std::make_shared<RemoteListenerContext>();
            context->listener = *listener;
            realtime->session->media_listener([context](std::shared_ptr<const RemoteMediaFrame> packet) {
                xmax_media_frame_impl borrowed{std::move(packet)};
                context->listener.on_frame(
                    context->listener.user_data,
                    &borrowed
                );
            });
            context->adopted = true;
        }
    );
}

/**
 * Retains a callback-borrowed or owned packet without copying its media bytes.
 *
 * @see xmax_media_frame_retain in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_media_frame_retain(
    xmax_media_frame frame,
    xmax_media_frame* out_frame
) {
    if (out_frame)
        *out_frame = nullptr;
    return protect(
        __func__,
        [&] {
            handle(frame);
            handle(out_frame);
            *out_frame = new xmax_media_frame_impl{frame->frame};
        }
    );
}

/**
 * Returns a borrowed view of one owned or callback-borrowed audio/video packet.
 *
 * @see xmax_media_frame_get_view in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_media_frame_get_view(
    xmax_media_frame frame,
    xmax_media_frame_view* view
) {
    return protect(
        __func__,
        [&] {
            handle(frame);
            sized(view);
            *view = frame->frame->view();
        }
    );
}

/**
 * Releases a retained packet and its media when its last owner is released.
 *
 * @see xmax_media_frame_release in xmax.h for the public contract.
 */
void XMAX_CALL xmax_media_frame_release(
    xmax_media_frame frame
) {
    delete frame;
}

/**
 * Binds external input and returns a receiver scoped to this source's lifetime.
 *
 * @see xmax_realtime_bind_external_video in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_bind_external_video(
    xmax_realtime realtime,
    const xmax_video_format* format,
    xmax_external_frame_sink* out_sink
) {
    if (out_sink)
        *out_sink = nullptr;
    return protect(
        __func__,
        [&] {
            handle(realtime);
            handle(format);
            handle(out_sink);
            auto sink = std::make_unique<xmax_external_frame_sink_impl>();
            sink->session = realtime->session;
            sink->format = *format;
            sink->binding = realtime->session->prepare(*format);
            *out_sink = sink.release();
        }
    );
}

/**
 * Returns the input format retained when this source receiver was created.
 *
 * @see xmax_external_frame_sink_get_format in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_external_frame_sink_get_format(
    xmax_external_frame_sink sink,
    xmax_video_format* format
) {
    return protect(
        __func__,
        [&] {
            handle(sink);
            handle(format);
            *format = sink->format;
        }
    );
}

/**
 * Reports whether the current source binding accepts preview and publication frames.
 *
 * @param[in] sink A valid source receiver.
 * @param[out] requesting Receives 1 for a live binding, including while disconnected, or 0 after failure or unbinding.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This is a demand signal, not a frame-rate limiter. A stale receiver never retains the controller.
 */
xmax_result XMAX_CALL xmax_external_frame_sink_is_requesting_frames(
    xmax_external_frame_sink sink,
    int32_t* requesting
) {
    if (requesting)
        *requesting = 0;
    return protect(
        __func__,
        [&] {
            handle(sink);
            handle(requesting);
            if (auto session = sink->session.lock())
                *requesting = session->requesting(sink->binding);
        }
    );
}

/**
 * Copies and submits a source-bound video frame without scaling, rotation, or pixel mirroring.
 *
 * @see xmax_external_frame_sink_push_video_frame in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_external_frame_sink_push_video_frame(
    xmax_external_frame_sink sink,
    const xmax_video_frame* frame
) {
    return protect(
        __func__,
        [&] {
            handle(sink);
            handle(frame);
            if (auto session = sink->session.lock())
                session->push(
                    *frame,
                    sink->binding
                );
        }
    );
}

/**
 * Reports an unrecoverable source failure and schedules cleanup of its media and realtime connection.
 *
 * @see xmax_external_frame_sink_report_error in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_external_frame_sink_report_error(
    xmax_external_frame_sink sink,
    const xmax_error_info* error
) {
    return protect(
        __func__,
        [&] {
            handle(sink);
            sized(error);
            require(
                error->code < 0 && std::memchr(
                                       error->message,
                                       0,
                                       sizeof(error->message)
                                   ),
                XMAX_INVALID_ARGUMENT,
                "A negative error code and NUL-terminated source error are required"
            );
            if (auto session = sink->session.lock())
                session->external_failure(
                    sink->binding,
                    Error{error->code, error->message, error->api_code, error->http_status}
                );
        }
    );
}

/**
 * Releases a source receiver without stopping caller-owned capture or unbinding the controller's source.
 *
 * @see xmax_external_frame_sink_release in xmax.h for the public contract.
 */
void XMAX_CALL xmax_external_frame_sink_release(
    xmax_external_frame_sink sink
) {
    delete sink;
}

/**
 * Binds caller-owned video and optional external PCM without microphone capture.
 *
 * @param[in] realtime The disconnected realtime controller.
 * @param[in] config The fixed source configuration.
 * @param[out] out_sink Receives an owned receiver, or NULL on failure.
 *
 * @return XMAX_OK on success, or a negative SDK error code.
 */
xmax_result XMAX_CALL xmax_realtime_bind_external_media(
    xmax_realtime realtime,
    const xmax_external_media_config* config,
    xmax_external_frame_sink* out_sink
) {
    if (out_sink)
        *out_sink = nullptr;
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(config);
            handle(out_sink);
            require(
                config->has_audio == 0 || config->has_audio == 1,
                XMAX_INVALID_ARGUMENT,
                "has_audio must be 0 or 1"
            );
            auto sink = std::make_unique<xmax_external_frame_sink_impl>();
            sink->session = realtime->session;
            sink->format = config->video_format;
            sink->binding = realtime->session->prepare(
                config->video_format,
                config->has_audio != 0
            );
            *out_sink = sink.release();
        }
    );
}

xmax_result XMAX_CALL xmax_realtime_create_external_video_stream(
    xmax_realtime realtime,
    const xmax_external_media_config* source,
    const xmax_video_format* processing_format,
    xmax_video_format* resolved_format,
    xmax_external_frame_sink* out_sink
) {
    return xmax_realtime_create_external_video_stream_with_encoding(
        realtime, source, processing_format, resolved_format, out_sink, nullptr
    );
}

/**
 * Creates an encoder-configured external source receiver.
 *
 * @see xmax_realtime_create_external_video_stream_with_encoding in xmax.h for the contract.
 */
xmax_result XMAX_CALL xmax_realtime_create_external_video_stream_with_encoding(
    xmax_realtime realtime,
    const xmax_external_media_config* source,
    const xmax_video_format* processing_format,
    xmax_video_format* resolved_format,
    xmax_external_frame_sink* out_sink,
    const xmax_encoding_config* encoding
) {
    if (out_sink)
        *out_sink = nullptr;
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(source);
            handle(resolved_format);
            if (encoding)
                sized(encoding);
            handle(out_sink);
            require(
                source->has_audio == 0 || source->has_audio == 1,
                XMAX_INVALID_ARGUMENT,
                "has_audio must be 0 or 1"
            );
            const auto resolved = MediaService::external_input(
                processing_format ? *processing_format : source->video_format,
                realtime->session->model_info()
            );
            auto sink = std::make_unique<xmax_external_frame_sink_impl>();
            sink->session = realtime->session;
            sink->format = source->video_format;
            sink->binding = realtime->session->prepare(
                source->video_format,
                source->has_audio != 0,
                resolved,
                encoding ? std::optional{*encoding} : std::nullopt
            );
            *resolved_format = resolved;
            *out_sink = sink.release();
        }
    );
}

xmax_result XMAX_CALL xmax_external_frame_sink_complete_startup(
    xmax_external_frame_sink sink
) {
    return protect(
        __func__,
        [&] {
            handle(sink);
            auto session = sink->session.lock();
            require(
                bool(session),
                XMAX_INVALID_STATE,
                "The external stream controller has been closed"
            );
            session->complete_external_stream(sink->binding);
        }
    );
}

xmax_result XMAX_CALL xmax_resolve_media_format(
    const char* model,
    const xmax_video_format* source,
    int32_t camera,
    xmax_video_format* resolved
) {
    return protect(__func__, [&] {
        handle(source);
        handle(resolved);
        require(camera == 0 || camera == 1, XMAX_INVALID_ARGUMENT, "Invalid media source kind");
        RealtimeModelDefinition::parse(model ? model : "x2.1");
        const auto info = RealtimeModelDefinition::capabilities;
        *resolved = camera ? MediaService::camera_input(*source, info)
                           : MediaService::external_input(*source, info);
    });
}

xmax_result XMAX_CALL xmax_resolve_generation_format(
    const char* model,
    const xmax_video_format* source,
    xmax_video_format* resolved
) {
    return protect(__func__, [&] {
        handle(source);
        handle(resolved);
        RealtimeModelDefinition::parse(model ? model : "x2.1");
        *resolved = MediaService::generation(*source, RealtimeModelDefinition::capabilities);
    });
}

xmax_result XMAX_CALL xmax_resolve_downlink_format(
    const xmax_video_format* source,
    double maximum_width,
    double maximum_height,
    xmax_video_format* resolved
) {
    return protect(__func__, [&] {
        handle(source);
        handle(resolved);
        *resolved = MediaService::target(*source, maximum_width, maximum_height);
    });
}

xmax_result XMAX_CALL xmax_realtime_list_microphones(
    xmax_realtime realtime,
    xmax_camera_list* devices
) {
    if (devices)
        *devices = nullptr;
    return protect(__func__, [&] {
        handle(realtime);
        handle(devices);
        auto list = std::make_unique<xmax_camera_list_impl>();
        list->devices = list_microphone_devices();
        *devices = list.release();
    });
}

xmax_result XMAX_CALL xmax_realtime_switch_microphone(
    xmax_realtime realtime,
    const char* device_id
) {
    return protect(__func__, [&] {
        handle(realtime);
        realtime->session->switch_microphone(device_id ? device_id : "");
    });
}

xmax_result XMAX_CALL xmax_realtime_set_microphone_enabled(
    xmax_realtime realtime,
    int32_t enabled
) {
    return protect(__func__, [&] {
        handle(realtime);
        require(enabled == 0 || enabled == 1, XMAX_INVALID_ARGUMENT, "Invalid microphone flag");
        realtime->session->set_microphone_enabled(enabled != 0);
    });
}

xmax_result XMAX_CALL xmax_realtime_get_microphone_enabled(
    xmax_realtime realtime,
    int32_t* enabled
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(enabled);
        *enabled = realtime->session->microphone_enabled() ? 1 : 0;
    });
}

xmax_result XMAX_CALL xmax_realtime_stop_local_camera_stream(
    xmax_realtime realtime
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            realtime->session->stop_media(XmaxRealtimeManager::StopMediaSource::Camera);
        }
    );
}

xmax_result XMAX_CALL xmax_realtime_stop_external_video_stream(
    xmax_realtime realtime
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            realtime->session->stop_media(XmaxRealtimeManager::StopMediaSource::External);
        }
    );
}

/**
 * Converts and uploads source-bound PCM, ignoring inactive and replaced sources.
 *
 * @param[in] sink The source-scoped receiver.
 * @param[in] frame The borrowed PCM samples and source timestamp.
 *
 * @return XMAX_OK on success or ignored input, or a negative SDK error code.
 */
xmax_result XMAX_CALL xmax_external_frame_sink_push_audio_frame(
    xmax_external_frame_sink sink,
    const xmax_external_audio_frame* frame
) {
    return protect(
        __func__,
        [&] {
            handle(sink);
            handle(frame);
            if (auto session = sink->session.lock())
                session->push_audio(
                    *frame,
                    sink->binding
                );
        }
    );
}

/**
 * Returns the latest video statistics without consuming realtime events.
 *
 * @param[in] realtime The realtime controller.
 * @param[in] direction Zero for uplink and one for downlink.
 * @param[in,out] statistics The destination, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative SDK error code.
 */
xmax_result XMAX_CALL xmax_realtime_get_video_statistics(
    xmax_realtime realtime,
    int32_t direction,
    xmax_video_statistics* statistics
) {
    return protect(
        __func__,
        [&] {
            handle(realtime);
            sized(statistics);
            *statistics = realtime->session->video_statistics(direction);
        }
    );
}

namespace {
struct BeautyListenerContext {
    xmax_video_beauty_error_listener listener{};
    bool adopted = false;
    ~BeautyListenerContext() {
        if (adopted && listener.release_user_data) {
            try { listener.release_user_data(listener.user_data); } catch (...) {}
        }
    }
};
}
/** Delegates validated beauty configuration to the shared render pipeline.
 * @see xmax_realtime_set_remote_video_beauty in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_set_remote_video_beauty(
    xmax_realtime realtime, const xmax_realtime_video_beauty_options* options) {
    return protect(__func__, [&] {
        handle(realtime);
        if (options) sized(options);
        realtime->session->remote_video_beauty(options ? std::optional<BeautyOptions>(*options) : std::nullopt);
    });
}
/** Adopts a dedicated recoverable-error callback with SDK-managed context lifetime.
 * @see xmax_realtime_set_remote_video_beauty_error_listener in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_realtime_set_remote_video_beauty_error_listener(
    xmax_realtime realtime, const xmax_video_beauty_error_listener* listener) {
    return protect(__func__, [&] {
        handle(realtime);
        if (!listener) { realtime->session->remote_video_beauty_error_listener({}); return; }
        sized(listener);
        require(listener->on_error != nullptr, XMAX_INVALID_ARGUMENT, "A beauty error callback is required");
        auto context = std::make_shared<BeautyListenerContext>();
        context->listener = *listener;
        realtime->session->remote_video_beauty_error_listener([context](Error error) {
            xmax_error_info info{}; info.struct_size = sizeof(info); info.code = error.code;
            info.api_code = error.api_code; info.http_status = error.http_status;
            const auto message = XmaxLogger::redact(error.what());
            std::memcpy(info.message, message.data(), std::min(message.size(), sizeof(info.message) - 1));
            context->listener.on_error(context->listener.user_data, &info);
        });
        context->adopted = true;
    });
}

/** Maps the typed account query and borrowed cancellation callback to the public C boundary.
 * @see xmax_client_get_credit_balance in xmax.h for the public contract.
 */
xmax_result XMAX_CALL xmax_client_get_credit_balance(
    xmax_client client, xmax_credit_balance* balance, const xmax_request_cancellation* cancellation
) {
    return protect(__func__, [&] {
        handle(client);
        sized(balance);
        Cancel cancelled = [] { return false; };
        if (cancellation) {
            sized(cancellation);
            require(cancellation->is_cancelled != nullptr, XMAX_INVALID_ARGUMENT, "A cancellation callback is required");
            const auto callback = *cancellation;
            cancelled = [callback] { return callback.is_cancelled(callback.user_data) != 0; };
        }
        const auto current = client->client.credit_balance(cancelled);
        balance->current_balance = current;
    });
}
