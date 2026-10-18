/* Copyright (c) 2026 XMAX.AI PTE. LTD. */
#ifndef XMAX_SDK_H
#define XMAX_SDK_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#if defined(XMAX_BUILDING_SDK)
#define XMAX_API __declspec(dllexport)
#else
#define XMAX_API __declspec(dllimport)
#endif
#define XMAX_CALL __cdecl
#else
#define XMAX_API __attribute__((visibility("default")))
#define XMAX_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define XMAX_ABI_VERSION 1u
#pragma pack(push, 8)
typedef struct xmax_client_impl* xmax_client;
typedef struct xmax_realtime_impl* xmax_realtime;
typedef struct xmax_frame_impl* xmax_frame;
typedef struct xmax_string_impl* xmax_string;
typedef struct xmax_camera_list_impl* xmax_camera_list;
typedef struct xmax_storage_impl* xmax_storage;
typedef struct xmax_uploaded_file_impl* xmax_uploaded_file;
typedef int32_t xmax_result;
enum {
    XMAX_OK = 0,
    XMAX_NO_EVENT = 1,
    XMAX_INVALID_ARGUMENT = -1,
    XMAX_INVALID_STATE = -2,
    XMAX_NETWORK_ERROR = -3,
    XMAX_API_ERROR = -4,
    XMAX_RTC_ERROR = -5,
    XMAX_TIMEOUT = -6,
    XMAX_CANCELLED = -7,
    XMAX_UNSUPPORTED = -8,
    XMAX_INTERNAL_ERROR = -9,
    XMAX_BUFFER_TOO_SMALL = -10,
    XMAX_MEDIA_ERROR = -11,
    XMAX_IO_ERROR = -12,
    XMAX_UNSAFE_IMAGE = -13
};
typedef enum xmax_state {
    XMAX_IDLE,
    XMAX_READY,
    XMAX_CONNECTING,
    XMAX_CONNECTED,
    XMAX_GENERATING,
    XMAX_DISCONNECTING
} xmax_state;
typedef enum xmax_pixel_format { XMAX_BGRA = 1, XMAX_NV12 = 2, XMAX_I420 = 3 } xmax_pixel_format;
typedef enum xmax_event_type {
    XMAX_EVENT_STATE = 1,
    XMAX_EVENT_ERROR = 2,
    XMAX_EVENT_STATISTICS = 3,
    XMAX_EVENT_TIMING = 4,
    XMAX_EVENT_QUALITY_CHANGE = 5, /* direction, quality=tier 1..5, effective dimensions and fps. */
    XMAX_EVENT_PERFORMANCE = 6     /* code: 1=limited, 0=recovered; dimensions/fps are recommendations. */
} xmax_event_type;
typedef struct xmax_client_config {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* api_key;      /* UTF-8; copied by create. May be empty for local setup. */
    const char* model;        /* x2.1.1-preview-cn / x2.1.1-preview-us; NULL = cn. */
    uint32_t http_timeout_ms; /* 0 = 15000. */
} xmax_client_config;
typedef struct xmax_video_format {
    uint32_t width, height, fps;
} xmax_video_format;
typedef struct xmax_video_frame {
    uint32_t struct_size;
    xmax_pixel_format pixel_format;
    uint32_t width, height;
    int64_t timestamp_us; /* Strictly increasing monotonic microseconds per input binding. */
    const uint8_t* planes[3];
    size_t strides[3];     /* Bytes, not pixels. */
    size_t plane_sizes[3]; /* Valid accessible bytes from each pointer. */
} xmax_video_frame;
typedef struct xmax_generation_context {
    uint32_t struct_size;
    const char* prompt;
    const char* reference_path; /* NULL omits reference image. */
    int32_t face_restoration;
} xmax_generation_context;
typedef struct xmax_event {
    uint32_t struct_size;
    xmax_event_type type;
    xmax_state state;
    xmax_result code;
    char message[512];
    char session_id[128];
    char task_id[128];
    /* Statistics/timing payload fields. -1 means unavailable, never synthetic data. */
    int32_t direction; /* 0 = uplink; 1 = downlink. */
    int32_t width, height, fps, bitrate_kbps, packet_loss_percent, quality, e2e_ms;
    double session_ms, rtc_join_ms, publish_ms, first_frame_ms, total_ms;
} xmax_event;
/* Additive APIs retain ABI version 1 and the original structure layouts. */
enum {
    XMAX_CAP_RTC = 1u << 0,
    XMAX_CAP_CAMERA = 1u << 1,
    XMAX_CAP_NATIVE_WINDOW = 1u << 2,
    XMAX_CAP_STORAGE = 1u << 3,
    XMAX_CAP_FRAME_INTERPOLATION = 1u << 4
};
enum { XMAX_CONTENT_FIT = 0, XMAX_CONTENT_FILL = 1 };
typedef struct xmax_capabilities {
    uint32_t struct_size, abi_version;
    uint64_t flags;
} xmax_capabilities;
typedef struct xmax_camera_info {
    uint32_t struct_size;
    const char* id; /* UTF-8; owned by the camera list until it is released. */
    const char* name;
    int32_t is_default;
} xmax_camera_info;
typedef struct xmax_camera_config {
    uint32_t struct_size;
    const char* device_id; /* NULL or empty selects the SDK's default camera. */
    xmax_video_format format;
    int32_t use_microphone;
} xmax_camera_config;
enum {
    XMAX_TRANSFER_IDLE,
    XMAX_TRANSFER_RUNNING,
    XMAX_TRANSFER_SUCCEEDED,
    XMAX_TRANSFER_FAILED,
    XMAX_TRANSFER_CANCELLED
};
typedef struct xmax_transfer_progress {
    uint32_t struct_size;
    int32_t state;
    uint64_t transferred, total; /* total == 0 when the server does not report a size. */
} xmax_transfer_progress;
typedef struct xmax_upload_options {
    uint32_t struct_size;
    const char* file_name;    /* Optional for file upload; required for data upload. */
    const char* content_type; /* NULL infers the image/video MIME type from the filename. */
    int32_t check_image_safety;
} xmax_upload_options;
typedef struct xmax_uploaded_file_info {
    uint32_t struct_size;
    const char* url;
    const char* object_key;
    const char* etag; /* NULL when unavailable. Strings live until file handle release. */
} xmax_uploaded_file_info;
enum { XMAX_ENCODER_AUTO = 0, XMAX_ENCODER_MAINTAIN_FRAMERATE = 1, XMAX_ENCODER_MAINTAIN_QUALITY = 2 };
typedef struct xmax_encoding_config {
    uint32_t struct_size;
    int32_t minimum_bitrate_kbps; /* -1=policy default, 0=unrestricted minimum. */
    int32_t maximum_bitrate_kbps; /* 0=policy estimate. */
    int32_t preference;
} xmax_encoding_config;
typedef struct xmax_quality_status {
    uint32_t struct_size;
    int32_t adaptive_enabled, uplink_level, downlink_level;
    xmax_video_format uplink, downlink;
} xmax_quality_status;
/* All exports contain C++ exceptions. last_error is thread-local and valid until
   the next SDK operation on that thread. Lifecycle calls block: use a worker
   thread. poll_event and acquire_frame are nonblocking. No application callbacks
   run inside the SDK. Quiesce all calls before destroying a handle. */
XMAX_API const char* XMAX_CALL xmax_version(void);
XMAX_API const char* XMAX_CALL xmax_last_error(void);
XMAX_API xmax_result XMAX_CALL xmax_client_create(const xmax_client_config*, xmax_client* out_client);
XMAX_API void XMAX_CALL xmax_client_destroy(xmax_client);
XMAX_API xmax_result XMAX_CALL xmax_client_has_rtc(xmax_client, int32_t* available);
XMAX_API xmax_result XMAX_CALL xmax_client_get_capabilities(xmax_client, xmax_capabilities*);
/* Enumerates without starting capture or allocating a cloud session. */
XMAX_API xmax_result XMAX_CALL xmax_client_list_cameras(xmax_client, xmax_camera_list*);
XMAX_API size_t XMAX_CALL xmax_camera_list_count(xmax_camera_list);
XMAX_API xmax_result XMAX_CALL xmax_camera_list_get(xmax_camera_list, size_t index, xmax_camera_info*);
XMAX_API void XMAX_CALL xmax_camera_list_release(xmax_camera_list);
XMAX_API xmax_result XMAX_CALL xmax_realtime_create(xmax_client, xmax_realtime* out_realtime);
XMAX_API void XMAX_CALL xmax_realtime_destroy(xmax_realtime);
/* External source is ready immediately. Camera/decoder ownership stays with the host. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_prepare_external(xmax_realtime, const xmax_video_format*);
/* SDK-owned capture and CPU preview. Blocks until the first real camera frame or error.
   Supported model inputs: 1920x1024 or 1024x1920. Does not allocate a cloud session. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_prepare_camera(xmax_realtime, const xmax_camera_config*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_switch_camera(xmax_realtime, const char* device_id);
XMAX_API xmax_result XMAX_CALL xmax_realtime_stop_media(xmax_realtime);
/* Connect independently; start/generate can reuse a connection. NULL generate context reuses conditions. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_connect(xmax_realtime, int32_t use_microphone);
XMAX_API xmax_result XMAX_CALL xmax_realtime_generate(xmax_realtime, const xmax_generation_context*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_state(xmax_realtime, xmax_event*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_format(xmax_realtime, xmax_video_format*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_remote_volume(xmax_realtime, float*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_encoding(xmax_realtime, const xmax_encoding_config*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_adaptive_quality(xmax_realtime, int32_t enabled);
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_quality(xmax_realtime, xmax_quality_status*);
/* Returns UNSUPPORTED when enabling without a native interpolation backend. Disabling is supported. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_interpolation(xmax_realtime, int32_t enabled);
XMAX_API xmax_result XMAX_CALL xmax_realtime_start(xmax_realtime, const xmax_generation_context*,
                                                   int32_t use_microphone);
XMAX_API xmax_result XMAX_CALL xmax_realtime_update(xmax_realtime, const xmax_generation_context*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_disconnect(xmax_realtime); /* Keeps local media/preview bound. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_close(xmax_realtime);      /* Releases binding and RTC. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_is_requesting_frames(xmax_realtime, int32_t* requesting);
/* Synchronous copy. Caller may reuse pixels immediately after return. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_push_video_frame(xmax_realtime, const xmax_video_frame*);
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_remote_volume(xmax_realtime, float volume);
XMAX_API xmax_result XMAX_CALL xmax_realtime_poll_event(xmax_realtime, xmax_event* out_event);
/* Latest-frame mailbox: replaces old unconsumed frames instead of building latency. */
XMAX_API xmax_result XMAX_CALL xmax_realtime_acquire_frame(xmax_realtime, xmax_frame* out_frame);
XMAX_API xmax_result XMAX_CALL xmax_realtime_acquire_local_frame(xmax_realtime, xmax_frame* out_frame);
XMAX_API xmax_result XMAX_CALL xmax_frame_get_view(xmax_frame, xmax_video_frame* out_view);
XMAX_API void XMAX_CALL xmax_frame_release(xmax_frame);
/* CPU fallback on Windows HWND owner thread. Caller drives WM_PAINT/repaint and retains the frame. */
XMAX_API xmax_result XMAX_CALL xmax_frame_render_window(xmax_frame, void* hwnd, int32_t content_mode);
/* Blocking storage operations: use a worker thread. One active operation per storage handle.
   cancel/progress may run concurrently. Quiesce operations before destroy. Client may be destroyed first. */
XMAX_API xmax_result XMAX_CALL xmax_storage_create(xmax_client, xmax_storage*);
XMAX_API void XMAX_CALL xmax_storage_destroy(xmax_storage);
XMAX_API void XMAX_CALL xmax_storage_cancel(xmax_storage);
XMAX_API xmax_result XMAX_CALL xmax_storage_get_progress(xmax_storage, xmax_transfer_progress*);
XMAX_API xmax_result XMAX_CALL xmax_storage_upload_file(xmax_storage, const char* utf8_path,
                                                        const xmax_upload_options*, xmax_uploaded_file*);
XMAX_API xmax_result XMAX_CALL xmax_storage_upload_data(xmax_storage, const uint8_t*, size_t length,
                                                        const xmax_upload_options*, xmax_uploaded_file*);
/* HTTPS, no redirects. Streams into a temporary file and publishes on success; fails if target exists. */
XMAX_API xmax_result XMAX_CALL xmax_storage_download(xmax_storage, const char* url,
                                                     const char* utf8_destination, uint64_t* byte_count);
XMAX_API xmax_result XMAX_CALL xmax_uploaded_file_get_info(xmax_uploaded_file, xmax_uploaded_file_info*);
XMAX_API void XMAX_CALL xmax_uploaded_file_release(xmax_uploaded_file);
/* Raw authenticated API requests for storage STS/safety and service integration.
   Only relative paths under the configured API root are accepted. */
XMAX_API xmax_result XMAX_CALL xmax_client_request_json(xmax_client, const char* method, const char* path,
                                                        const char* body, xmax_string* out_json);
XMAX_API const char* XMAX_CALL xmax_string_data(xmax_string);
XMAX_API void XMAX_CALL xmax_string_release(xmax_string);
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(xmax_state) == 4 && sizeof(xmax_pixel_format) == 4 && sizeof(xmax_event_type) == 4,
              "XmaxSDK requires 32-bit C enums; disable short-enum compiler flags");
#else
typedef char xmax_require_32bit_enums
    [(sizeof(xmax_state) == 4 && sizeof(xmax_pixel_format) == 4 && sizeof(xmax_event_type) == 4) ? 1 : -1];
#endif
#ifdef __cplusplus
}
#endif
#endif
