//
//  xmax.h
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

/**
 * @file xmax.h
 *
 * The stable UTF-8 C ABI, with structures packed on eight-byte boundaries.
 *
 * Initialize struct_size before using sized structures and specify XMAX_ABI_VERSION in creation
 * configurations. Release SDK allocations through their matching release functions, never your app's
 * allocator. Negative results indicate failure; XMAX_NO_EVENT means an empty event or frame queue. C++
 * exceptions do not cross this boundary; read xmax_last_error on the same thread after failure. Lifecycle
 * and storage operations block; use worker threads and finish concurrent handle use before destruction.
 */

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

/**
 * The opaque client handle, created by xmax_client_create and released by xmax_client_destroy.
 */
typedef struct xmax_client_impl* xmax_client;

/**
 * The opaque realtime handle, created by xmax_realtime_create and released by xmax_realtime_destroy.
 */
typedef struct xmax_realtime_impl* xmax_realtime;

/**
 * A frame handle whose pixels remain accessible until xmax_frame_release.
 */
typedef struct xmax_frame_impl* xmax_frame;

/**
 * The opaque owner of returned UTF-8 text, released by xmax_string_release.
 */
typedef struct xmax_string_impl* xmax_string;

/**
 * The opaque camera enumeration snapshot, released by xmax_camera_list_release.
 */
typedef struct xmax_camera_list_impl* xmax_camera_list;

/**
 * The opaque storage transfer controller, released by xmax_storage_destroy.
 */
typedef struct xmax_storage_impl* xmax_storage;

/**
 * The opaque upload metadata owner, released by xmax_uploaded_file_release.
 */
typedef struct xmax_uploaded_file_impl* xmax_uploaded_file;

/**
 * An SDK result: 0 indicates success, positive values indicate no pending data, and negative values indicate
 * failure.
 */
typedef int32_t xmax_result;

/**
 * SDK result codes; nonnegative values do not indicate failure.
 */
enum {

    /**
     * The operation succeeded.
     */
    XMAX_OK = 0,

    /**
     * No event, log, or video frame is available; this is not a failure.
     */
    XMAX_NO_EVENT = 1,

    /**
     * An argument is null, out of range, malformed, or has an incompatible structure version.
     */
    XMAX_INVALID_ARGUMENT = -1,

    /**
     * The current lifecycle state does not permit the operation.
     */
    XMAX_INVALID_STATE = -2,

    /**
     * Network byte transport failed.
     */
    XMAX_NETWORK_ERROR = -3,

    /**
     * The service returned an API error or invalid response envelope.
     */
    XMAX_API_ERROR = -4,

    /**
     * RTC engine, room connection, or native signaling failed.
     */
    XMAX_RTC_ERROR = -5,

    /**
     * The operation exceeded its deadline.
     */
    XMAX_TIMEOUT = -6,

    /**
     * The operation was cancelled or invalidated.
     */
    XMAX_CANCELLED = -7,

    /**
     * The SDK build or platform does not provide the requested capability.
     */
    XMAX_UNSUPPORTED = -8,

    /**
     * An unclassified internal SDK failure occurred.
     */
    XMAX_INTERNAL_ERROR = -9,

    /**
     * The output buffer is too small.
     */
    XMAX_BUFFER_TOO_SMALL = -10,

    /**
     * Device capture, pixel layout, or media processing failed.
     */
    XMAX_MEDIA_ERROR = -11,

    /**
     * Local file access or destination publication failed.
     */
    XMAX_IO_ERROR = -12,

    /**
     * The reference image failed the requested safety check.
     */
    XMAX_UNSAFE_IMAGE = -13,

    /**
     * API credentials are absent or empty.
     */
    XMAX_INVALID_API_KEY = -14,

    /**
     * Session data or heartbeat state is invalid.
     */
    XMAX_SESSION_ERROR = -15,

    /**
     * The system denied camera access.
     */
    XMAX_CAMERA_PERMISSION_DENIED = -16,

    /**
     * The system denied microphone access.
     */
    XMAX_MICROPHONE_PERMISSION_DENIED = -17,

    /**
     * File upload failed.
     */
    XMAX_UPLOAD_ERROR = -18,

    /**
     * File download failed.
     */
    XMAX_DOWNLOAD_ERROR = -19
};

/**
 * The realtime media, connection, and generation lifecycle state.
 */
typedef enum xmax_state {

    /**
     * No usable local media is bound.
     */
    XMAX_IDLE,

    /**
     * Local media is ready without an active RTC connection.
     */
    XMAX_READY,

    /**
     * Server-session allocation and RTC join are in progress.
     */
    XMAX_CONNECTING,

    /**
     * RTC is connected without an active generation task.
     */
    XMAX_CONNECTED,

    /**
     * Generation is active and receiving its result stream.
     */
    XMAX_GENERATING,

    /**
     * Operations are being cancelled and connection resources released.
     */
    XMAX_DISCONNECTING,

    /**
     * Local media is being prepared; connection and generation are not yet permitted.
     */
    XMAX_PREPARING = 6
} xmax_state;

/**
 * The pixel layout determining plane count and row-stride interpretation.
 */
typedef enum xmax_pixel_format {

    /**
     * Single-plane BGRA, four bytes per pixel.
     */
    XMAX_BGRA = 1,
    /**
     * Two-plane 4:2:0: Y followed by interleaved UV.
     */
    XMAX_NV12 = 2,
    /**
     * Three-plane 4:2:0: Y, U, and V.
     */
    XMAX_I420 = 3
} xmax_pixel_format;

/**
 * The application event type determining which payload fields are valid.
 */
typedef enum xmax_event_type {

    /**
     * A lifecycle state change.
     */
    XMAX_EVENT_STATE = 1,

    /**
     * An SDK failure described by code and message.
     */
    XMAX_EVENT_ERROR = 2,

    /**
     * Actual transport or video statistics, with direction and valid fields in the payload.
     */
    XMAX_EVENT_STATISTICS = 3,

    /**
     * Startup timing for session creation, room join, publication, and first result.
     */
    XMAX_EVENT_TIMING = 4,

    /**
     * A quality-level change with direction, level, and effective format.
     */
    XMAX_EVENT_QUALITY_CHANGE = 5, /* direction, quality=tier 1..5, effective dimensions and fps. */

    /**
     * A device performance event; code 1 means constrained and 0 means recovered.
     */
    XMAX_EVENT_PERFORMANCE = 6 /* code: 1=limited, 0=recovered; dimensions/fps are recommendations. */
} xmax_event_type;

/**
 * The combinable process-wide logging option bits.
 */
enum {

    /**
     * Disables business and performance logging.
     */
    XMAX_LOG_NONE = 0,
    /**
     * Enables lifecycle, service, media, and other business diagnostics.
     */
    XMAX_LOG_BUSINESS = 1u << 0,
    /**
     * Enables actual frame-rate, processing-time, transport, and quality-performance diagnostics.
     */
    XMAX_LOG_PERFORMANCE = 1u << 1,
    /**
     * Enables both business and performance logging.
     */
    XMAX_LOG_ALL = 3u
};

/**
 * The log severity.
 */
typedef enum xmax_log_level {

    /**
     * Detailed debug diagnostics.
     */
    XMAX_LOG_DEBUG,

    /**
     * Normal operation and state information.
     */
    XMAX_LOG_INFO,

    /**
     * A condition requiring attention that may not cause failure.
     */
    XMAX_LOG_WARNING,

    /**
     * An operation failure or unrecoverable condition.
     */
    XMAX_LOG_ERROR
} xmax_log_level;

/**
 * The output language for log messages.
 */
typedef enum xmax_log_language {

    /**
     * Chinese log messages.
     */
    XMAX_LOG_CHINESE,
    /**
     * English log messages.
     */
    XMAX_LOG_ENGLISH
} xmax_log_language;

/**
 * A redacted log record owning its text buffers.
 */
typedef struct xmax_log_record {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The record severity, using the XMAX_LOG_* severity values.
     */
    xmax_log_level level;

    /**
     * The business and performance logging bitmask.
     */
    uint32_t options;

    /**
     * Reserved; your app must keep this field zero.
     */
    uint32_t reserved;

    /**
     * The record creation timestamp, in Unix UTC microseconds.
     */
    int64_t timestamp_us;

    /**
     * A process-wide increasing sequence number; dropped records can leave gaps.
     */
    uint64_t sequence;

    /**
     * The NUL-terminated category label.
     */
    char category[32];

    /**
     * The NUL-terminated UTF-8 diagnostic text.
     */
    char message[4096];

} xmax_log_record;

/**
 * The client's ABI version, credentials, model, and timeout; strings are copied during creation.
 */
typedef struct xmax_client_config {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The SDK ABI version; use XMAX_ABI_VERSION when creating configurations.
     */
    uint32_t abi_version;

    /**
     * The UTF-8 API credential, copied at creation; empty permits local preparation.
     */
    const char* api_key;

    /**
     * The realtime model identifier; only x2.1 is supported and NULL selects the default.
     */
    const char* model;

    /**
     * The HTTP timeout in milliseconds; 0 selects the 15000-millisecond default.
     */
    uint32_t http_timeout_ms;

} xmax_client_config;

/**
 * Video dimensions and frame rate for external input or a camera's RTC encoding target.
 *
 * Camera targets support 1024x1920 and 1920x1024 at [1, 60] fps. The 1080p landscape and portrait
 * presets remain accepted as compatibility aliases for these targets. External input accepts even
 * dimensions in [4, 8192] totaling at most 2100000 pixels, without restricting input to camera presets.
 * Capture preserves native frame dimensions; RTC handles target aspect ratio and resolution.
 */
typedef struct xmax_video_format {

    /**
     * The video width, in pixels.
     */
    uint32_t width;

    /**
     * The video height, in pixels.
     */
    uint32_t height;

    /**
     * The video frame rate, in frames per second.
     */
    uint32_t fps;

} xmax_video_format;

/**
 * A read-only video-plane view with format, monotonic timestamp, byte strides, and accessible sizes.
 */
typedef struct xmax_video_frame {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The video pixel format determining plane count and byte layout.
     */
    xmax_pixel_format pixel_format;

    /**
     * The video width, in pixels.
     */
    uint32_t width;

    /**
     * The video height, in pixels.
     */
    uint32_t height;

    /**
     * The strictly increasing monotonic timestamp, in microseconds, within one input binding.
     */
    int64_t timestamp_us;

    /**
     * Borrowed read-only plane pointers, valid for the input call or owning frame handle's lifetime.
     */
    const uint8_t* planes[3];

    /**
     * The row stride of each plane, in bytes rather than pixels.
     */
    size_t strides[3];

    /**
     * The accessible bytes from each plane pointer, used to validate row layout.
     */
    size_t plane_sizes[3];

} xmax_video_frame;

/**
 * The sample representation of interleaved external PCM.
 */
typedef enum xmax_audio_sample_format {

    /**
     * Signed little-endian 16-bit samples.
     */
    XMAX_AUDIO_PCM16 = 0,

    /**
     * Little-endian IEEE 754 32-bit samples, finite and in [-1, 1].
     */
    XMAX_AUDIO_FLOAT32 = 1

} xmax_audio_sample_format;

/**
 * Borrowed external PCM on the same monotonic timeline as the source video.
 */
typedef struct xmax_external_audio_frame {

    /**
     * The structure size in bytes; initialize with sizeof.
     */
    uint32_t struct_size;

    /**
     * The interleaved sample representation, fixed for this binding.
     */
    xmax_audio_sample_format sample_format;

    /**
     * The sampling frequency in [8000, 192000] Hz, fixed for this binding.
     */
    uint32_t sample_rate;

    /**
     * The channel count in [1, 8], fixed for this binding; channels are mixed equally.
     */
    uint32_t channels;

    /**
     * The samples per channel, from 1 through sample_rate; at most one second per call.
     */
    uint32_t samples_per_channel;

    /**
     * The first sample timestamp, in nonnegative monotonic microseconds shared with video.
     */
    int64_t timestamp_us;

    /**
     * The contiguous interleaved samples, borrowed until submission returns.
     */
    const void* data;

    /**
     * The exact sample byte count; samples_per_channel * channels * bytes per sample.
     */
    size_t data_size;

} xmax_external_audio_frame;

/**
 * Declares external video and optional PCM upload without microphone capture or local audio playback.
 */
typedef struct xmax_external_media_config {

    /**
     * The structure size in bytes; initialize with sizeof.
     */
    uint32_t struct_size;

    /**
     * The exact video input dimensions and frame rate, retained without pixel scaling.
     */
    xmax_video_format video_format;

    /**
     * Whether the source supplies PCM audio; must be 0 or 1 and remains fixed until unbinding.
     */
    int32_t has_audio;

} xmax_external_media_config;

/**
 * A directional RTC video snapshot. Unavailable numeric metrics are -1; available is 0 after cleanup.
 */
typedef struct xmax_video_statistics {

    /**
     * The structure size in bytes; initialize with sizeof.
     */
    uint32_t struct_size;

    /**
     * Whether this direction has a current video statistics snapshot.
     */
    int32_t available;

    /**
     * The transport direction: 0 for uplink and 1 for downlink.
     */
    int32_t direction;

    /**
     * The result user account for downlink; empty for uplink or when unavailable.
     */
    char user_id[128];

    /**
     * The encoded uplink or received downlink width, in pixels.
     */
    int32_t width;

    /**
     * The encoded uplink or received downlink height, in pixels.
     */
    int32_t height;

    /**
     * The local capture frame rate; unavailable for downlink.
     */
    int32_t capture_fps;

    /**
     * The local encoder output frame rate; unavailable for downlink.
     */
    int32_t encode_fps;

    /**
     * The outgoing transmission frame rate; unavailable for downlink.
     */
    int32_t send_fps;

    /**
     * The received decoder output frame rate; unavailable for uplink.
     */
    int32_t decode_fps;

    /**
     * The native RTC render frame rate, excluding SDK processing; unavailable for uplink.
     */
    int32_t render_fps;

    /**
     * The sent or received video bitrate, in kbps.
     */
    int32_t bitrate_kbps;

    /**
     * The sent or received video packet loss, in [0, 100].
     */
    int32_t packet_loss_percent;

    /**
     * The RTC media end-to-end delay, in milliseconds; unavailable for uplink.
     */
    int32_t e2e_ms;

} xmax_video_statistics;

/**
 * The prompt, optional reference image, and face-restoration conditions; strings remain valid throughout the
 * call.
 */
typedef struct xmax_generation_context {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The generation prompt, encoded as UTF-8.
     */
    const char* prompt;

    /**
     * An optional reference image path or service-supported URL; NULL omits it.
     */
    const char* reference_path;

    /**
     * A nonzero value enables face restoration.
     */
    int32_t face_restoration;

} xmax_generation_context;

/**
 * A lifecycle, failure, quality, or statistics event; negative metrics mean unavailable.
 */
typedef struct xmax_event {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The event type determining which remaining fields are valid.
     */
    xmax_event_type type;

    /**
     * The realtime state at event creation or snapshot acquisition.
     */
    xmax_state state;

    /**
     * The SDK error code or performance-warning flag, depending on event type.
     */
    xmax_result code;

    /**
     * The NUL-terminated UTF-8 diagnostic text.
     */
    char message[512];

    /**
     * The NUL-terminated session identifier, empty before session creation.
     */
    char session_id[128];

    /**
     * The NUL-terminated task identifier, empty before generation starts.
     */
    char task_id[128];

    /**
     * The transport direction: 0 for uplink or 1 for downlink.
     */
    int32_t direction;

    /**
     * The event video's width, in pixels.
     */
    int32_t width;

    /**
     * The event video's height, in pixels.
     */
    int32_t height;

    /**
     * The event video's frame rate, in frames per second.
     */
    int32_t fps;

    /**
     * The actual video bitrate, in kbps.
     */
    int32_t bitrate_kbps;

    /**
     * The video packet-loss rate, as a percentage.
     */
    int32_t packet_loss_percent;

    /**
     * The network-quality rating or video quality level, depending on event type.
     */
    int32_t quality;

    /**
     * The end-to-end latency, in milliseconds.
     */
    int32_t e2e_ms;

    /**
     * The server-session creation duration, in milliseconds.
     */
    double session_ms;

    /**
     * The RTC room join duration, in milliseconds.
     */
    double rtc_join_ms;

    /**
     * The first actual local publication duration, in milliseconds.
     */
    double publish_ms;

    /**
     * The duration from generation start to its first result frame, in milliseconds.
     */
    double first_frame_ms;

    /**
     * The duration from this generation startup request to its committed first result, in milliseconds.
     * Existing-connection idle time and camera preparation are excluded. Failure timing reports elapsed
     * startup time with a negative code and leaves unfinished stages at -1.
     */
    double total_ms;

} xmax_event;

/**
 * SDK capability flags; queries include only implemented capabilities.
 */
enum {

    /**
     * The build provides native RTC connection and video transport.
     */
    XMAX_CAP_RTC = 1u << 0,

    /**
     * The build provides native camera capture.
     */
    XMAX_CAP_CAMERA = 1u << 1,

    /**
     * The build provides native HWND video rendering.
     */
    XMAX_CAP_NATIVE_WINDOW = 1u << 2,

    /**
     * The build provides uploads, downloads, and storage operations.
     */
    XMAX_CAP_STORAGE = 1u << 3,

    /**
     * The build provides a native video interpolation backend.
     */
    XMAX_CAP_FRAME_INTERPOLATION = 1u << 4,

    /**
     * Provides trajectory coordinate mapping, task signaling, and effect-data queries.
     */
    XMAX_CAP_TRAJECTORY = 1u << 5,

    /**
     * The RTC build provides copied result-user PCM and video output on a shared rendering timeline.
     */
    XMAX_CAP_REMOTE_MEDIA_OUTPUT = 1u << 6,

    /**
     * The RTC build supports source-bound external PCM upload.
     */
    XMAX_CAP_EXTERNAL_AUDIO_INPUT = 1u << 7
};

/**
 * The native window's video aspect-ratio and cropping policy.
 */
enum {

    /**
     * Fits the entire video while preserving aspect ratio, padding unused space.
     */
    XMAX_CONTENT_FIT = 0,
    /**
     * Fills the target while preserving aspect ratio, cropping overflow from the center.
     */
    XMAX_CONTENT_FILL = 1
};

/**
 * The SDK's ABI version and implemented capability flags.
 */
typedef struct xmax_capabilities {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The SDK ABI version; use XMAX_ABI_VERSION when creating configurations.
     */
    uint32_t abi_version;

    /**
     * The implemented capability bitmask, interpreted using XMAX_CAP_* constants.
     */
    uint64_t flags;

} xmax_capabilities;

/**
 * Borrows camera strings from an enumeration snapshot; invalid after snapshot release.
 */
typedef struct xmax_camera_info {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The camera identifier used to select this device explicitly.
     */
    const char* id;

    /**
     * The camera's display name; not a unique device identifier.
     */
    const char* name;

    /**
     * Nonzero for the default device in this enumeration; 0 for other devices.
     */
    int32_t is_default;

} xmax_camera_info;

/**
 * The camera device, RTC encoding target, and microphone policy at connection time.
 */
typedef struct xmax_camera_config {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The UTF-8 camera identifier; NULL or empty selects the default device.
     */
    const char* device_id;

    /**
     * The RTC encoding target; an all-zero format selects the model's camera default (1024x1920 at 30 fps
     * for x2.1). Frame views report actual capture dimensions.
     */
    xmax_video_format format;

    /**
     * A nonzero value enables the microphone when connecting to RTC.
     */
    int32_t use_microphone;

} xmax_camera_config;

/**
 * The storage transfer lifecycle state.
 */
enum {

    /**
     * No transfer is active.
     */
    XMAX_TRANSFER_IDLE,

    /**
     * A transfer is in progress.
     */
    XMAX_TRANSFER_RUNNING,

    /**
     * The transfer completed successfully.
     */
    XMAX_TRANSFER_SUCCEEDED,

    /**
     * The transfer stopped after a failure.
     */
    XMAX_TRANSFER_FAILED,

    /**
     * The transfer stopped after cancellation.
     */
    XMAX_TRANSFER_CANCELLED
};

/**
 * Transfer progress containing state, completed bytes, and total bytes.
 */
typedef struct xmax_transfer_progress {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The current XMAX_TRANSFER_* state.
     */
    int32_t state;

    /**
     * The transferred byte count.
     */
    uint64_t transferred;

    /**
     * The total transfer size, in bytes; 0 when unknown.
     */
    uint64_t total;

} xmax_transfer_progress;

/**
 * The upload filename, optional MIME type, and image safety-check policy.
 */
typedef struct xmax_upload_options {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The object filename; optional for file uploads and required for byte uploads.
     */
    const char* file_name;

    /**
     * An optional MIME type; NULL or empty infers it from the filename extension.
     */
    const char* content_type;

    /**
     * A nonzero value requests a safety check for image uploads.
     */
    int32_t check_image_safety;

} xmax_upload_options;

/**
 * Borrows the uploaded URL, object key, and ETag; invalid after result-handle release.
 */
typedef struct xmax_uploaded_file_info {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The URL of the successfully uploaded file.
     */
    const char* url;

    /**
     * The storage object key used to locate the uploaded object.
     */
    const char* object_key;

    /**
     * The borrowed object ETag, or NULL if absent from the service response.
     */
    const char* etag;

} xmax_uploaded_file_info;

/**
 * The encoding degradation preference under bandwidth or performance constraints.
 */
enum {

    /**
     * Lets the native encoder choose the degradation preference.
     */
    XMAX_ENCODER_AUTO = 0,
    /**
     * Prioritizes frame rate under bandwidth or performance constraints.
     */
    XMAX_ENCODER_MAINTAIN_FRAMERATE = 1,
    /**
     * Prioritizes quality under bandwidth or performance constraints.
     */
    XMAX_ENCODER_MAINTAIN_QUALITY = 2
};

/**
 * The uplink bitrate limits and quality-versus-frame-rate degradation preference.
 */
typedef struct xmax_encoding_config {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * The minimum bitrate in kbps; -1 selects the policy default and 0 omits the minimum limit.
     */
    int32_t minimum_bitrate_kbps;

    /**
     * The maximum bitrate in kbps; 0 uses the current format's policy estimate.
     */
    int32_t maximum_bitrate_kbps;

    /**
     * The encoding degradation preference, using XMAX_ENCODER_* constants.
     */
    int32_t preference;

} xmax_encoding_config;

/**
 * Independent horizontal mirroring for local preview, remote display, and publication; all disabled by
 * default.
 */
typedef struct xmax_video_mirror_config {

    /**
     * The structure size; initialize with sizeof(xmax_video_mirror_config).
     */
    uint32_t struct_size;

    /**
     * Whether to mirror local preview; accepts only 0 or 1 and leaves capture pixels unchanged.
     */
    int32_t local_preview;

    /**
     * Whether to mirror remote display; accepts only 0 or 1 and leaves decoded pixels unchanged.
     */
    int32_t remote_view;

    /**
     * Whether RTC mirrors uplink encoding; accepts only 0 or 1. Enabling sends mirrored video to the
     * service.
     */
    int32_t published_video;

} xmax_video_mirror_config;

/**
 * The adaptation setting, directional quality levels, and effective formats.
 */
typedef struct xmax_quality_status {

    /**
     * The structure size in bytes. Initialize it with sizeof for this structure.
     */
    uint32_t struct_size;

    /**
     * A nonzero value enables directional quality adaptation.
     */
    int32_t adaptive_enabled;

    /**
     * The current uplink level, in [1, 5].
     */
    int32_t uplink_level;

    /**
     * The current downlink level, in [1, 5].
     */
    int32_t downlink_level;

    /**
     * The effective uplink dimensions and frame rate.
     */
    xmax_video_format uplink;

    /**
     * The effective downlink dimensions and frame rate.
     */
    xmax_video_format downlink;

} xmax_quality_status;

/**
 * The SDK service environment, independent of the realtime generation model.
 */
typedef enum xmax_environment {

    /**
     * The China service environment.
     */
    XMAX_ENVIRONMENT_CHINA = 0,

    /**
     * The global service environment.
     */
    XMAX_ENVIRONMENT_GLOBAL = 1

} xmax_environment;

/**
 * Camera encoding-resolution presets; capture may use native device dimensions.
 */
typedef enum xmax_camera_preset {

    /**
     * No camera stream or no matching preset.
     */
    XMAX_CAMERA_PRESET_NONE = 0,

    /**
     * The 1080p landscape compatibility alias for the model's 1920x1024 target.
     */
    XMAX_CAMERA_PRESET_1920_1080 = 1,

    /**
     * The 1080p portrait compatibility alias for the model's 1024x1920 target.
     */
    XMAX_CAMERA_PRESET_1080_1920 = 2,

    /**
     * A reserved square preset; the current model rejects this value.
     */
    XMAX_CAMERA_PRESET_1408_1408 = 3,

    /**
     * A reserved 3:4 preset; the current model rejects this value.
     */
    XMAX_CAMERA_PRESET_1152_1536 = 4,

    /**
     * A reserved 4:3 preset; the current model rejects this value.
     */
    XMAX_CAMERA_PRESET_1536_1152 = 5,

    /**
     * The model's 1920 by 1024 landscape camera target.
     */
    XMAX_CAMERA_PRESET_1920_1024 = 6,

    /**
     * The model's 1024 by 1920 portrait camera target.
     */
    XMAX_CAMERA_PRESET_1024_1920 = 7

} xmax_camera_preset;

/**
 * The reason for a realtime state change; starting a normal operation clears previous failures.
 */
typedef enum xmax_realtime_reason {

    /**
     * A normal operation or explicit stop.
     */
    XMAX_REASON_NORMAL = 0,

    /**
     * The display orientation changed and requires reconfiguration.
     */
    XMAX_REASON_ORIENTATION_CHANGED = 1,

    /**
     * An operation or runtime failure; see associated error details.
     */
    XMAX_REASON_FAILURE = 2

} xmax_realtime_reason;

/**
 * Realtime model capabilities; strings reference static SDK storage and must not be released.
 */
typedef struct xmax_model_info {

    /**
     * Initialize with sizeof(xmax_model_info).
     */
    uint32_t struct_size;

    /**
     * The server-supported model identifier.
     */
    const char* identifier;

    /**
     * The model's display name.
     */
    const char* display_name;

    /**
     * Storage for up to five supported formats; only the first format_count entries are populated.
     */
    xmax_video_format supported_formats[5];

    /**
     * The valid element count in supported_formats.
     */
    uint32_t format_count;

    /**
     * The minimum generation canvas pixel count when no fixed-size list exists; input frames need not be
     * enlarged.
     */
    uint32_t minimum_input_pixels;

    /**
     * The maximum pixel count for local video frames.
     */
    uint32_t maximum_input_pixels;

    /**
     * The canvas dimension alignment when no fixed-size list exists.
     */
    uint32_t input_size_alignment;

    /**
     * The default frame rate, in frames per second.
     */
    uint32_t default_frame_rate;

    /**
     * The default camera encoding target.
     */
    xmax_video_format default_camera_format;

} xmax_model_info;

/**
 * The global client configuration; realtime controllers select their models independently.
 */
typedef struct xmax_client_options {

    /**
     * Initialize with sizeof(xmax_client_options).
     */
    uint32_t struct_size;

    /**
     * Use XMAX_ABI_VERSION.
     */
    uint32_t abi_version;

    /**
     * The UTF-8 credential copied at creation; empty permits local preparation only.
     */
    const char* api_key;

    /**
     * The service environment for general and realtime APIs.
     */
    xmax_environment environment;

    /**
     * The logging bitmask; 0 disables logging. Language follows the environment and affects all clients in
     * the process.
     */
    uint32_t logger_options;

    /**
     * The HTTP timeout in milliseconds; 0 selects 15000.
     */
    uint32_t http_timeout_ms;

} xmax_client_options;

/**
 * The realtime configuration; Windows does not enable remote frame interpolation.
 */
typedef struct xmax_realtime_config {

    /**
     * Initialize with sizeof(xmax_realtime_config).
     */
    uint32_t struct_size;

    /**
     * The model identifier copied at creation; NULL selects x2.1.
     */
    const char* model;

} xmax_realtime_config;

/**
 * Creation options for a realtime controller with an explicit quality-adaptation policy.
 */
typedef struct xmax_realtime_options {

    /**
     * Initialize with sizeof(xmax_realtime_options).
     */
    uint32_t struct_size;

    /**
     * The copied model identifier; NULL selects x2.1.
     */
    const char* model;

    /**
     * Whether automatic quality adaptation is enabled; accepts 0 or 1. Disabled connections start at L1,
     * ignore persisted history, and still permit manual quality changes.
     */
    int32_t adaptive_quality_enabled;

} xmax_realtime_options;

/**
 * An owned remote media packet, or a callback-borrowed packet retained with xmax_media_frame_retain.
 */
typedef struct xmax_media_frame_impl* xmax_media_frame;

/**
 * A source-bound video receiver; stale bindings discard frames and errors without affecting newer sources.
 */
typedef struct xmax_external_frame_sink_impl* xmax_external_frame_sink;

/**
 * The content carried by one remote media packet.
 */
typedef enum xmax_media_type {

    /**
     * Signed, little-endian, interleaved 16-bit PCM from the result user before remote-user mixing.
     */
    XMAX_MEDIA_AUDIO = 1,

    /**
     * A decoded result video frame in its decoded pixel orientation.
     */
    XMAX_MEDIA_VIDEO = 2

} xmax_media_type;

/**
 * Rational media time in seconds, expressed as value divided by timescale. A zero timescale is unavailable.
 */
typedef struct xmax_media_time {

    /**
     * The signed count of time units.
     */
    int64_t value;

    /**
     * The positive number of units per second, or zero for an unavailable time.
     */
    int32_t timescale;

} xmax_media_time;

/**
 * Borrowed PCM bytes owned by a media packet; retain the packet to keep these bytes after a callback.
 */
typedef struct xmax_audio_frame {

    /**
     * The structure size, populated by the SDK.
     */
    uint32_t struct_size;

    /**
     * The number of samples per second in each channel.
     */
    uint32_t sample_rate;

    /**
     * The number of interleaved channels, either one or two.
     */
    uint32_t channels;

    /**
     * The number of samples in each channel in this packet.
     */
    uint32_t samples_per_channel;

    /**
     * The read-only PCM16 bytes, valid until their owning packet is released.
     */
    const uint8_t* data;

    /**
     * The PCM byte count, equal to samples_per_channel multiplied by channels and two.
     */
    size_t data_size;

} xmax_audio_frame;

/**
 * A borrowed view of one audio or video packet on a shared receiver rendering timeline.
 *
 * Delivery order is serialized but is not presentation-time order across tracks. Use a shared recording
 * origin and schedule both tracks by presentation_time. Display mirroring does not modify output pixels.
 */
typedef struct xmax_media_frame_view {

    /**
     * Initialize with sizeof(xmax_media_frame_view).
     */
    uint32_t struct_size;

    /**
     * The populated content variant; the other variant is cleared.
     */
    xmax_media_type type;

    /**
     * The opaque nonzero identity shared by audio and video of one result binding; reconnecting changes it.
     */
    uint64_t stream_id;

    /**
     * Whether this track starts, changes format or clock, or follows discarded output. Gaps retain their
     * original timeline positions and may require a new recording segment.
     */
    int32_t is_discontinuous;

    /**
     * The first audio sample or video presentation time on the common receiver rendering timeline.
     */
    xmax_media_time presentation_time;

    /**
     * The exact audio duration; video duration is unavailable when its timescale is zero.
     */
    xmax_media_time duration;

    /**
     * The PCM variant for XMAX_MEDIA_AUDIO; otherwise cleared.
     */
    xmax_audio_frame audio;

    /**
     * The read-only video variant for XMAX_MEDIA_VIDEO; otherwise cleared.
     */
    xmax_video_frame video;

} xmax_media_frame_view;

/**
 * Receives one callback-borrowed packet on the controller's serial background queue.
 *
 * @param[in] user_data The context supplied at registration.
 * @param[in] frame The packet, valid until this callback returns. Retain it to use it asynchronously; do
 * not release the callback-borrowed handle itself.
 */
typedef void(XMAX_CALL* xmax_remote_media_frame_callback)(
    void* user_data,
    xmax_media_frame frame
);

/**
 * Releases a successfully registered listener's context after its final in-flight callback completes.
 *
 * @param[in] user_data The registered context. This callback can run on any SDK or registration thread.
 */
typedef void(XMAX_CALL* xmax_listener_context_release)(
    void* user_data
);

/**
 * A remote media callback and its optional SDK-managed context lifetime.
 */
typedef struct xmax_remote_media_listener {

    /**
     * Initialize with sizeof(xmax_remote_media_listener).
     */
    uint32_t struct_size;

    /**
     * The non-null callback invoked in delivery order. Forward packets promptly; slow callbacks discard
     * later output when the queue reaches 256 packets or 32 MiB, including the packet in flight.
     */
    xmax_remote_media_frame_callback on_frame;

    /**
     * The context passed to on_frame and release_user_data.
     */
    void* user_data;

    /**
     * The optional context releaser. Ownership transfers only on successful registration. Without a
     * releaser, your app must keep the context alive until every in-flight callback has completed.
     */
    xmax_listener_context_release release_user_data;

} xmax_remote_media_listener;

/**
 * SDK error details; api_code and http_status are 0 when absent.
 */
typedef struct xmax_error_info {

    /**
     * Initialize with sizeof(xmax_error_info).
     */
    uint32_t struct_size;

    /**
     * The unified SDK error code.
     */
    xmax_result code;

    /**
     * The service error code.
     */
    int32_t api_code;

    /**
     * The HTTP response status.
     */
    int32_t http_status;

    /**
     * The redacted NUL-terminated UTF-8 error message.
     */
    char message[1024];

} xmax_error_info;

/**
 * An event with state reason and error details, consuming the legacy event queue.
 */
typedef struct xmax_event_info {

    /**
     * Initialize with sizeof(xmax_event_info).
     */
    uint32_t struct_size;

    /**
     * The underlying state, statistics, or diagnostic event.
     */
    xmax_event event;

    /**
     * The reason for a state or error event; normal for other event types.
     */
    xmax_realtime_reason reason;

    /**
     * The associated state error or error-event details.
     */
    xmax_error_info error;

} xmax_event_info;

/**
 * A 2D point; input uses viewport coordinates and output trajectories use normalized coordinates in [0, 1].
 */
typedef struct xmax_point {

    /**
     * The horizontal coordinate, increasing to the right.
     */
    double x;

    /**
     * The vertical coordinate, increasing downward.
     */
    double y;

} xmax_point;

/**
 * A trajectory sample whose dimensions and points use the same viewport coordinate units.
 */
typedef struct xmax_interaction_frame {

    /**
     * Initialize with sizeof(xmax_interaction_frame).
     */
    uint32_t struct_size;

    /**
     * The points in sampling order, borrowed during submission only.
     */
    const xmax_point* points;

    /**
     * The sampled point count, in [1, 256].
     */
    size_t point_count;

    /**
     * The viewport width, a finite positive value.
     */
    double viewport_width;

    /**
     * The viewport height, a finite positive value.
     */
    double viewport_height;

    /**
     * The displayed video's pixel width, matching the actual displayed frame.
     */
    uint32_t displayed_video_width;

    /**
     * The displayed video's pixel height.
     */
    uint32_t displayed_video_height;

    /**
     * The layout mode: XMAX_CONTENT_FIT or XMAX_CONTENT_FILL.
     */
    int32_t content_mode;

} xmax_interaction_frame;

/**
 * Returns the model's supported formats, pixel limits, and defaults.
 *
 * @param[in] model The model identifier; NULL selects x2.1.
 * @param[in,out] info The destination structure, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_model_get_info(
    const char* model,
    xmax_model_info* info
);

/**
 * Copies the global configuration and creates a client without starting capture or a cloud session.
 *
 * @param[in] options The global configuration, including the ABI version and service environment.
 * @param[out] out_client Receives the client, or NULL on failure. Release it with xmax_client_destroy.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_client_create_with_options(
    const xmax_client_options* options,
    xmax_client* out_client
);

/**
 * Creates a realtime controller with its own model configuration.
 *
 * @param[in] client A non-null client handle.
 * @param[in] config The realtime configuration, copied during creation.
 * @param[out] out_realtime Receives the realtime handle, or NULL on failure. Release it with
 * xmax_realtime_destroy.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_create_with_config(
    xmax_client client,
    const xmax_realtime_config* config,
    xmax_realtime* out_realtime
);

/**
 * Creates a realtime controller with an initial quality-adaptation policy.
 *
 * @param[in] client A valid client handle.
 * @param[in] options The creation options, copied during this call.
 * @param[out] out_realtime Receives the controller; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note Release the controller with xmax_realtime_destroy. Existing creation functions enable adaptation.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_create_with_options(
    xmax_client client,
    const xmax_realtime_options* options,
    xmax_realtime* out_realtime
);

/**
 * Returns the calling thread's most recent structured error without clearing it.
 *
 * @param[in,out] info The destination structure, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_last_error_info(
    xmax_error_info* info
);

/**
 * Returns the current state, state reason, and associated error.
 *
 * @param[in] realtime The realtime handle.
 * @param[in,out] info The destination structure, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_state_info(
    xmax_realtime realtime,
    xmax_event_info* info
);

/**
 * Consumes the oldest event with its state reason and error details without blocking.
 *
 * @param[in] realtime The realtime handle.
 * @param[in,out] info The destination structure, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note An empty queue returns XMAX_NO_EVENT. This shares the xmax_realtime_poll_event queue; use one
 * consumer interface.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_poll_event_info(
    xmax_realtime realtime,
    xmax_event_info* info
);

/**
 * Disconnects for the specified reason while retaining local media preview.
 *
 * @param[in] realtime The realtime handle.
 * @param[in] reason Normal termination or orientation change; the SDK reports failure reasons.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_disconnect_with_reason(
    xmax_realtime realtime,
    xmax_realtime_reason reason
);

/**
 * Updates a ready, disconnected camera's encoding format while retaining preview and device selection.
 *
 * @param[in] realtime The realtime handle.
 * @param[in] format The new model-supported format; existing bitrate and encoding preferences are retained.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This call blocks; use a worker thread. Preparation failure attempts to restore the previous format.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_update_camera_format(
    xmax_realtime realtime,
    const xmax_video_format* format
);

/**
 * Changes a ready camera's resolution while retaining its frame rate and encoding configuration.
 *
 * @param[in] realtime The realtime handle.
 * @param[in] preset The target resolution preset; NONE is not permitted.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_update_camera_preset(
    xmax_realtime realtime,
    xmax_camera_preset preset
);

/**
 * Returns the camera's encoding format and preset, independent of network quality changes.
 *
 * @param[in] realtime The realtime handle.
 * @param[out] format Receives the format; cleared when no camera stream exists.
 * @param[out] preset Receives the preset; NONE when no camera stream exists.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_camera_format(
    xmax_realtime realtime,
    xmax_video_format* format,
    xmax_camera_preset* preset
);

/**
 * Sets the uplink level during generation without disabling subsequent network adaptation.
 *
 * @param[in] realtime The realtime handle.
 * @param[in] level The target level, in [1, 5]; 1 provides the highest quality.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This call blocks and preserves the previous level on failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_uplink_quality_level(
    xmax_realtime realtime,
    int32_t level
);

/**
 * Sets the downlink level during generation without disabling subsequent network adaptation.
 *
 * @param[in] realtime The realtime handle.
 * @param[in] level The target level, in [1, 5]; 1 provides the highest quality.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This call blocks and preserves the previous level on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_set_downlink_quality_level(
    xmax_realtime realtime,
    int32_t level
);

/**
 * Maps trajectory samples to video coordinates and asynchronously sends tracks for the current task.
 *
 * @param[in] realtime The realtime handle with an active generation task.
 * @param[in] frame The viewport trajectory samples, copied during this call.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note Pending samples are coalesced to the latest values; aspect-fit margins are ignored. Success
 * acknowledges submission, not server processing.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_submit_interaction(
    xmax_realtime realtime,
    const xmax_interaction_frame* frame
);

/**
 * Copies unexpired normalized trajectory points for custom effect rendering.
 *
 * @param[in] realtime The realtime handle.
 * @param[out] points The destination point array; may be NULL when capacity is 0.
 * @param[in] capacity The array capacity; at most 256 elements are needed.
 * @param[out] count Receives the required point count; insufficient capacity returns XMAX_BUFFER_TOO_SMALL.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_get_trajectory(
    xmax_realtime realtime,
    xmax_point* points,
    size_t capacity,
    size_t* count
);

/**
 * Renders a result frame and the default trajectory effect in an HWND.
 *
 * @param[in] realtime The realtime handle; the trajectory belongs to the sampled task.
 * @param[in] frame The frame to display, valid for this call.
 * @param[in] window A Windows HWND owned by the calling thread.
 * @param[in] content_mode The aspect-fit or aspect-fill mode, matching the trajectory sampling
 * configuration.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note Call on the HWND owner thread. Other UI frameworks can query trajectory points and draw their own
 * effects.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_render_window(
    xmax_realtime realtime,
    xmax_frame frame,
    void* window,
    int32_t content_mode
);

/**
 * Returns the SDK's semantic version.
 *
 * @return A static UTF-8 version string; do not modify or release it.
 */
XMAX_API const char* XMAX_CALL xmax_version(void);

/**
 * Returns the calling thread's most recent SDK diagnostic message.
 *
 * @return A borrowed NUL-terminated UTF-8 string; do not release it. Copy immediately to retain it.
 *
 * @note Valid until the next SDK operation on this thread. Diagnostics are isolated between threads.
 */
XMAX_API const char* XMAX_CALL xmax_last_error(void);

/**
 * Configures process-wide logging options and language shared by all clients.
 *
 * @param[in] options A bitmask combining XMAX_LOG_BUSINESS and XMAX_LOG_PERFORMANCE; XMAX_LOG_NONE disables
 * logging.
 * @param[in] language The Chinese or English language for subsequent records.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Thread-safe. Logging is disabled by default; changing the configuration clears pending records.
 */
XMAX_API xmax_result XMAX_CALL xmax_logger_configure(
    uint32_t options,
    xmax_log_language language
);

/**
 * Returns the process-wide logging configuration.
 *
 * @param[out] options A non-null destination for the enabled logging bitmask.
 * @param[out] language A non-null destination for the output language.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Thread-safe. Creating a client does not reset this configuration.
 */
XMAX_API xmax_result XMAX_CALL xmax_logger_get_config(
    uint32_t* options,
    xmax_log_language* language
);

/**
 * Acquires the oldest process-wide log record without blocking.
 *
 * @param[in,out] record A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 *
 * @note Use a single consumer for the process-wide queue, which retains at most the latest 256 records.
 */
XMAX_API xmax_result XMAX_CALL xmax_logger_poll(
    xmax_log_record* record
);

/**
 * Copies the configuration and creates a client without starting a camera or cloud session.
 *
 * @param[in] config The client configuration, with struct_size and XMAX_ABI_VERSION initialized; empty
 * credentials permit local preparation.
 * @param[out] out_client A non-null destination for the client handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release the handle with xmax_client_destroy. Input strings are copied during creation.
 */
XMAX_API xmax_result XMAX_CALL xmax_client_create(
    const xmax_client_config* config,
    xmax_client* out_client
);

/**
 * Releases the client handle and its owned resources.
 *
 * @param[in] client The handle to release; may be NULL.
 *
 * @note Finish concurrent handle use before release. Existing child controllers independently retain their
 * shared configuration.
 */
XMAX_API void XMAX_CALL xmax_client_destroy(
    xmax_client client
);

/**
 * Reports whether this SDK build includes an RTC backend.
 *
 * @param[in] client A non-null client handle.
 * @param[out] available A non-null destination receiving 1 when available, or 0 otherwise.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
XMAX_API xmax_result XMAX_CALL xmax_client_has_rtc(
    xmax_client client,
    int32_t* available
);

/**
 * Returns this build's capability flags and ABI version.
 *
 * @param[in] client A non-null client handle.
 * @param[in,out] capabilities A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
XMAX_API xmax_result XMAX_CALL
xmax_client_get_capabilities(
    xmax_client client,
    xmax_capabilities* capabilities
);

/**
 * Enumerates camera devices without starting capture or a cloud session.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out_list A non-null destination for the enumeration snapshot; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release the snapshot with xmax_camera_list_release.
 */
XMAX_API xmax_result XMAX_CALL xmax_client_list_cameras(
    xmax_client client,
    xmax_camera_list* out_list
);

/**
 * Returns the number of devices in a camera enumeration snapshot.
 *
 * @param[in] list The enumeration snapshot; may be NULL.
 *
 * @return The device count, or 0 for a NULL handle.
 */
XMAX_API size_t XMAX_CALL xmax_camera_list_count(
    xmax_camera_list list
);

/**
 * Returns information about a camera in the enumeration snapshot.
 *
 * @param[in] list A valid, non-null enumeration snapshot.
 * @param[in] index The zero-based device index, less than the device count.
 * @param[in,out] info A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Device strings belong to the snapshot and remain valid until it is released.
 */
XMAX_API xmax_result XMAX_CALL
xmax_camera_list_get(
    xmax_camera_list list,
    size_t index,
    xmax_camera_info* info
);

/**
 * Releases a camera enumeration snapshot and its device strings.
 *
 * @param[in] list The snapshot to release; may be NULL.
 */
XMAX_API void XMAX_CALL xmax_camera_list_release(
    xmax_camera_list list
);

/**
 * Creates an independent realtime controller without connecting to the cloud.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out_realtime A non-null destination for the realtime handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release with xmax_realtime_destroy. The handle independently retains the client's service
 * configuration.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_create(
    xmax_client client,
    xmax_realtime* out_realtime
);

/**
 * Closes and releases the realtime controller's session, RTC, and media resources.
 *
 * @param[in] realtime The realtime handle to release; may be NULL.
 *
 * @note Finish concurrent handle use before release. This call closes native resources and waits for an
 * in-flight application media callback; destruction from that callback lets it finish normally.
 */
XMAX_API void XMAX_CALL xmax_realtime_destroy(
    xmax_realtime realtime
);

/**
 * Binds an external input supplied continuously by your app.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Bind before connecting; binding stops the previous local
 * media input.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_prepare_external(
    xmax_realtime realtime,
    const xmax_video_format* format
);

/**
 * Starts SDK camera capture and waits for the first real video frame.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] config The camera configuration with struct_size initialized; an empty identifier selects the
 * default camera, and an all-zero video format selects the model's camera default.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Only local video is prepared; no server session is created.
 * Microphone capture follows the configuration when connecting.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_prepare_camera(
    xmax_realtime realtime,
    const xmax_camera_config* config
);

/**
 * Switches the active camera while retaining the connection and restoring generation conditions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] device_id A non-null UTF-8 device identifier; empty selects the default camera.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. It waits for the new camera; failure cleans up the connection
 * and local media.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_switch_camera(
    xmax_realtime realtime,
    const char* device_id
);

/**
 * Releases local capture and input bindings after disconnection.
 *
 * @param[in] realtime A non-null realtime controller handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Disconnect any active connection first.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_stop_media(
    xmax_realtime realtime
);

/**
 * Creates a server session and joins RTC without starting generation.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] use_microphone A nonzero value enables the microphone when joining the room.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Prepare local media first. The camera configuration's
 * microphone option also applies.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_connect(
    xmax_realtime realtime,
    int32_t use_microphone
);

/**
 * Starts generation with supplied or cached conditions and waits for the first result frame.
 *
 * A newer generation request cancels an unfinished one with XMAX_CANCELLED. The newer request executes
 * after the previous request's cleanup; only successful requests replace cached generation conditions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] context Generation conditions whose strings remain valid until this call returns. NULL reuses
 * cached conditions; the first generation requires conditions.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Ready media connects first if needed; supply external frames
 * continuously from another thread.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_generate(
    xmax_realtime realtime,
    const xmax_generation_context* context
);

/**
 * Returns a state and session-identifier snapshot without consuming queued events.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in,out] state A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This query may wait for the serialized lifecycle queue.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_state(
    xmax_realtime realtime,
    xmax_event* state
);

/**
 * Returns the external input format or camera RTC encoding target; local frame views report actual capture
 * dimensions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[out] format A non-null destination for the input format.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This query may wait for the serialized lifecycle queue.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_format(
    xmax_realtime realtime,
    xmax_video_format* format
);

/**
 * Returns the remote playback volume for the current or next connection.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[out] volume A non-null destination for the volume in [0, 1].
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The initial volume is 0. This query does not wait for connection or generation operations.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_remote_volume(
    xmax_realtime realtime,
    float* volume
);

/**
 * Configures uplink bitrate limits and encoding degradation preference.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] config The encoding configuration with struct_size initialized; -1 selects the minimum-bitrate
 * policy default, and 0 selects the estimated maximum.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. An active connection updates its native encoder.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_set_encoding(
    xmax_realtime realtime,
    const xmax_encoding_config* config
);

/**
 * Configures three independent mirror settings, retained through disconnection and reconnection.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] config A non-null mirror configuration with struct_size initialized.
 *
 * @return XMAX_OK on success; RTC update failure preserves the previous configuration and returns a negative
 * error code.
 *
 * @note Call before media preparation or after connecting. This may wait for lifecycle execution or RTC
 * updates; use a worker thread. Display settings apply to subsequently acquired frames. Native window
 * rendering handles them automatically; custom renderers read xmax_frame_get_mirrored.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_set_video_mirror(
    xmax_realtime realtime,
    const xmax_video_mirror_config* config
);

/**
 * Returns the session's retained mirror configuration.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in,out] config A non-null destination configuration with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_get_video_mirror(
    xmax_realtime realtime,
    xmax_video_mirror_config* config
);

/**
 * Enables or disables independent uplink and downlink quality adaptation.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] enabled Nonzero enables adaptation; 0 disables it. Changes reset the network observation
 * window.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_adaptive_quality(
    xmax_realtime realtime,
    int32_t enabled
);

/**
 * Returns directional quality levels and effective video formats.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in,out] quality A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This query may wait for the serialized lifecycle queue.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_get_quality(
    xmax_realtime realtime,
    xmax_quality_status* quality
);

/**
 * Requests enabling or disabling frame interpolation.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] enabled Nonzero requests frame interpolation.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Enabling without a native interpolation backend returns XMAX_UNSUPPORTED; disabling can still
 * succeed.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_interpolation(
    xmax_realtime realtime,
    int32_t enabled
);

/**
 * Connects and starts generation with initial conditions, waiting for the first result frame.
 *
 * A newer generation request cancels an unfinished one with XMAX_CANCELLED. The newer request executes
 * after the previous request's cleanup; only successful requests replace cached generation conditions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] context Non-null generation conditions whose strings remain valid until this synchronous call
 * returns.
 * @param[in] use_microphone A nonzero value enables the microphone when connecting.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Prepare media first and submit external frames continuously
 * from another thread.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_start(
    xmax_realtime realtime,
    const xmax_generation_context* context,
    int32_t use_microphone
);

/**
 * Replaces the running generation task's conditions.
 *
 * A newer generation request cancels an unfinished one with XMAX_CANCELLED. The newer request executes
 * after the previous request's cleanup; only successful requests replace cached generation conditions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] context Non-null generation conditions whose strings remain valid until this synchronous call
 * returns.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Generation must already be active.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_update(
    xmax_realtime realtime,
    const xmax_generation_context* context
);

/**
 * Disconnects the server session and RTC while retaining local preview and cached conditions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. It also requests cancellation of in-flight lifecycle
 * operations.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_disconnect(
    xmax_realtime realtime
);

/**
 * Disconnects and releases local media and cached generation conditions.
 *
 * @param[in] realtime A non-null realtime controller handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. The handle remains valid for preparing media and starting
 * another session.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_close(
    xmax_realtime realtime
);

/**
 * Reports whether your app should currently supply external video frames.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[out] requesting A non-null destination for the request flag; camera input always returns 0.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The flag stays true while a live external source drives disconnected preview. It becomes false
 * after unbinding or source failure. This flag does not pace frames or wait for lifecycle work.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_is_requesting_frames(
    xmax_realtime realtime,
    int32_t* requesting
);

/**
 * Copies and submits an external video frame synchronously.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] frame A valid video view with struct_size initialized, matching bound dimensions and a strictly
 * increasing timestamp.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The source pixels can be reused after return. External frames are rejected with SDK camera input;
 * disconnected external input still supports local preview.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_push_video_frame(
    xmax_realtime realtime,
    const xmax_video_frame* frame
);

/**
 * Sets the remote playback volume for the current or next connection.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in] volume A finite value in [0, 1]; 0 mutes playback.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Independent of connection and generation operations. The setting is retained before result-user
 * audio is available and applied before audio subscription starts. Set after preparing the local stream
 * to override its initial playback volume.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_remote_volume(
    xmax_realtime realtime,
    float volume
);

/**
 * Consumes the oldest pending application event without blocking.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[in,out] out_event A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_poll_event(
    xmax_realtime realtime,
    xmax_event* out_event
);

/**
 * Acquires the latest remote result frame without blocking.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[out] out_frame A non-null destination for the frame handle. Pixels remain valid until release; no
 * frame or failure clears the handle to NULL.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 *
 * @note Release with xmax_frame_release. New frames replace unconsumed frames.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_acquire_frame(
    xmax_realtime realtime,
    xmax_frame* out_frame
);

/**
 * Acquires the latest local preview frame without blocking.
 *
 * @param[in] realtime A non-null realtime controller handle.
 * @param[out] out_frame A non-null destination for the frame handle. Pixels remain valid until release; no
 * frame or failure clears the handle to NULL.
 *
 * @return XMAX_OK when data is available, XMAX_NO_EVENT when no data is pending, or a negative error code on
 * failure.
 *
 * @note Release with xmax_frame_release. Acquisition does not wait for capture.
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_acquire_local_frame(
    xmax_realtime realtime,
    xmax_frame* out_frame
);

/**
 * Returns a borrowed view of the frame's pixel layout.
 *
 * @param[in] frame A valid, non-null frame handle.
 * @param[in,out] out_view A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note The view does not retain pixels. Do not access it after releasing the frame handle.
 */
XMAX_API xmax_result XMAX_CALL xmax_frame_get_view(
    xmax_frame frame,
    xmax_video_frame* out_view
);

/**
 * Returns the horizontal display-mirror flag captured at acquisition; pixels retain capture or decode
 * orientation.
 *
 * @param[in] frame A valid, non-null frame handle.
 * @param[out] mirrored A non-null destination receiving 0 for normal display or 1 for horizontal mirroring.
 *
 * @return XMAX_OK on success; a negative error code on failure, with the output set to 0.
 *
 * @note Custom renderers apply this flag at presentation; do not republish display-mirrored pixels. SDK
 * native window rendering already handles the flag; do not mirror twice.
 */
XMAX_API xmax_result XMAX_CALL xmax_frame_get_mirrored(
    xmax_frame frame,
    int32_t* mirrored
);

/**
 * Releases the frame handle and its retained pixel storage.
 *
 * @param[in] frame The frame handle to release; may be NULL.
 */
XMAX_API void XMAX_CALL xmax_frame_release(
    xmax_frame frame
);

/**
 * Renders a video frame on the target HWND's owner thread.
 *
 * @param[in] frame A valid, non-null frame handle.
 * @param[in] hwnd The target Windows HWND cast to void*.
 * @param[in] content_mode XMAX_CONTENT_FIT for aspect fit, or XMAX_CONTENT_FILL for centered aspect fill.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Call on the HWND owner thread. Platforms without native window rendering return XMAX_UNSUPPORTED.
 */
XMAX_API xmax_result XMAX_CALL xmax_frame_render_window(
    xmax_frame frame,
    void* hwnd,
    int32_t content_mode
);

/**
 * Creates an independent storage transfer controller.
 *
 * @param[in] client A non-null client handle.
 * @param[out] out_storage A non-null destination for the storage handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Release with xmax_storage_destroy. The handle independently retains the client configuration.
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_create(
    xmax_client client,
    xmax_storage* out_storage
);

/**
 * Releases a storage transfer controller.
 *
 * @param[in] storage The storage handle to release; may be NULL.
 *
 * @note Cancel and wait for in-flight transfers and finish concurrent handle use before release.
 */
XMAX_API void XMAX_CALL xmax_storage_destroy(
    xmax_storage storage
);

/**
 * Requests cancellation of the current storage transfer.
 *
 * @param[in] storage The storage handle; may be NULL.
 *
 * @note May be called from another thread; does not wait for transfer completion.
 */
XMAX_API void XMAX_CALL xmax_storage_cancel(
    xmax_storage storage
);

/**
 * Returns a thread-safe transfer state and byte-progress snapshot.
 *
 * @param[in] storage A non-null storage handle. Each instance permits one active transfer.
 * @param[in,out] progress A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Thread-safe. Total bytes are 0 when unknown.
 */
XMAX_API xmax_result XMAX_CALL
xmax_storage_get_progress(
    xmax_storage storage,
    xmax_transfer_progress* progress
);

/**
 * Uploads an encoded local image or video file.
 *
 * @param[in] storage A non-null storage handle. Each instance permits one active transfer.
 * @param[in] utf8_path A non-null UTF-8 local file path.
 * @param[in] options Optional upload settings; NULL infers filename and MIME type and disables image safety
 * checks.
 * @param[out] out_file A non-null destination for the upload result handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Images are limited to 20 MiB and other media to 256 MiB.
 * Release results with xmax_uploaded_file_release.
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_upload_file(
    xmax_storage storage,
    const char* utf8_path,
    const xmax_upload_options* options,
    xmax_uploaded_file* out_file
);

/**
 * Uploads encoded file bytes, not raw video pixels.
 *
 * @param[in] storage A non-null storage handle. Each instance permits one active transfer.
 * @param[in] data Non-null encoded file bytes, valid throughout this call.
 * @param[in] length The accessible byte count, greater than 0 and within the media-type limit.
 * @param[in] options The upload configuration with struct_size initialized and a filename provided.
 * @param[out] out_file A non-null destination for the upload result handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Keep input data valid until return; release results with
 * xmax_uploaded_file_release.
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_upload_data(
    xmax_storage storage,
    const uint8_t* data,
    size_t length,
    const xmax_upload_options* options,
    xmax_uploaded_file* out_file
);

/**
 * Downloads HTTPS content to a local file that does not yet exist.
 *
 * @param[in] storage A non-null storage handle. Each instance permits one active transfer.
 * @param[in] url A non-null absolute HTTPS URL; redirects are not accepted.
 * @param[in] utf8_destination A non-null UTF-8 destination path; an existing file causes failure.
 * @param[out] byte_count A non-null destination for the completed file's byte count.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Downloads are limited to 256 MiB; the destination becomes
 * visible only on success.
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_download(
    xmax_storage storage,
    const char* url,
    const char* utf8_destination,
    uint64_t* byte_count
);

/**
 * Returns borrowed metadata from an upload result.
 *
 * @param[in] file A non-null upload result handle.
 * @param[in,out] info A non-null destination structure with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note Strings remain valid until the upload result is released; a missing ETag has a NULL pointer.
 */
XMAX_API xmax_result XMAX_CALL
xmax_uploaded_file_get_info(
    xmax_uploaded_file file,
    xmax_uploaded_file_info* info
);

/**
 * Releases an upload result and its metadata strings.
 *
 * @param[in] file The result handle to release; may be NULL.
 */
XMAX_API void XMAX_CALL xmax_uploaded_file_release(
    xmax_uploaded_file file
);

/**
 * Performs an authenticated JSON request under the model's API root.
 *
 * @param[in] client A non-null client handle.
 * @param[in] method The HTTP method: GET, POST, PUT, or DELETE.
 * @param[in] path A relative API path; absolute URLs to other hosts are not permitted.
 * @param[in] body An optional UTF-8 JSON body; NULL omits the body.
 * @param[out] out_json A non-null destination for the response data payload as a JSON string handle.
 *
 * @return XMAX_OK on success, or a negative error code on failure. Use xmax_last_error for details.
 *
 * @note This call blocks; use a worker thread. Release the text with xmax_string_release; the full response
 * envelope is not returned.
 */
XMAX_API xmax_result XMAX_CALL xmax_client_request_json(
    xmax_client client,
    const char* method,
    const char* path,
    const char* body,
    xmax_string* out_json
);

/**
 * Returns borrowed UTF-8 content from an SDK string.
 *
 * @param[in] string The SDK string handle; may be NULL.
 *
 * @return Borrowed NUL-terminated text, or NULL for an empty handle; do not modify or release it.
 *
 * @note The text remains valid until its string handle is released.
 */
XMAX_API const char* XMAX_CALL xmax_string_data(
    xmax_string string
);

/**
 * Releases an SDK string and its text storage.
 *
 * @param[in] string The string handle to release; may be NULL.
 */
XMAX_API void XMAX_CALL xmax_string_release(
    xmax_string string
);

/**
 * Sets serialized remote PCM and video delivery without changing playback volume or subscriptions.
 *
 * @param[in] realtime A valid realtime controller.
 * @param[in] listener The listener configuration, copied on success; NULL clears it and queued output.
 *
 * @return XMAX_OK on success, or a negative error code if observation cannot be configured.
 *
 * @note This call may wait for active lifecycle work; use a worker thread. Register before generation or
 * replace while connected. Callbacks use a dedicated background worker
 * and may call SDK methods. An in-flight callback may finish after replacement or removal. Context ownership
 * transfers only on successful registration; release_user_data runs after its final in-flight callback.
 * Retaining packets in your app does not count toward the SDK's 256-packet / 32-MiB queue limit.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_remote_media_frame_listener(
    xmax_realtime realtime,
    const xmax_remote_media_listener* listener
);

/**
 * Retains a callback-borrowed or owned packet without copying its media bytes.
 *
 * @param[in] frame The valid packet to retain.
 * @param[out] out_frame Receives a new owned handle; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note Release the returned handle with xmax_media_frame_release.
 */
XMAX_API xmax_result XMAX_CALL xmax_media_frame_retain(
    xmax_media_frame frame,
    xmax_media_frame* out_frame
);

/**
 * Returns a borrowed view of one owned or callback-borrowed audio/video packet.
 *
 * @param[in] frame The packet whose media remains retained throughout view use.
 * @param[in,out] view The destination structure, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note Pixel and PCM pointers remain valid only while their owning packet remains retained. Do not modify
 * them, release them separately, or infer that one packet contains a synchronized audio/video pair.
 */
XMAX_API xmax_result XMAX_CALL xmax_media_frame_get_view(
    xmax_media_frame frame,
    xmax_media_frame_view* view
);

/**
 * Releases a retained packet and its media when its last owner is released.
 *
 * @param[in] frame An owned packet handle or NULL; never pass the callback-borrowed handle itself.
 */
XMAX_API void XMAX_CALL xmax_media_frame_release(
    xmax_media_frame frame
);

/**
 * Binds external input and returns a receiver scoped to this source's lifetime.
 *
 * @param[in] realtime The controller, disconnected and either idle or media-ready.
 * @param[in] format The external input's exact dimensions and frame rate, copied during this call.
 * @param[out] out_sink Receives an owned source receiver; cleared to NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This call blocks; use a worker thread. Binding does not wait for a frame or preview. Source capture,
 * permissions, and pacing remain your
 * app's responsibility. Supply frames while startup is waiting for its first result. Replaced bindings
 * silently discard subsequent frames and errors. Release the receiver with xmax_external_frame_sink_release.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_bind_external_video(
    xmax_realtime realtime,
    const xmax_video_format* format,
    xmax_external_frame_sink* out_sink
);

/**
 * Returns the input format retained when this source receiver was created.
 *
 * @param[in] sink A valid source receiver.
 * @param[out] format Receives the required input dimensions and frame rate, even after unbinding.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 */
XMAX_API xmax_result XMAX_CALL
xmax_external_frame_sink_get_format(
    xmax_external_frame_sink sink,
    xmax_video_format* format
);

/**
 * Reports whether the current source binding accepts preview and publication frames.
 *
 * @param[in] sink A valid source receiver.
 * @param[out] requesting Receives 1 for a live binding, including while disconnected, or 0 after failure or unbinding.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This is a demand signal, not a frame-rate limiter. The receiver does not retain the controller between calls.
 */
XMAX_API xmax_result XMAX_CALL
xmax_external_frame_sink_is_requesting_frames(
    xmax_external_frame_sink sink,
    int32_t* requesting
);

/**
 * Copies and submits a source-bound video frame without scaling, rotation, or pixel mirroring.
 *
 * @param[in] sink The receiver returned when this source was bound.
 * @param[in] frame The borrowed BGRA, NV12, or I420 pixels matching the bound format. Negative, duplicate, or
 * decreasing monotonic microsecond timestamps are ignored. Pixels can be reused after return but must
 * not be changed concurrently.
 *
 * @return XMAX_OK for submission, ignored timestamps, or a stale binding; XMAX_MEDIA_ERROR for invalid
 * current pixels, which also fails the binding and schedules connection/media cleanup.
 */
XMAX_API xmax_result XMAX_CALL
xmax_external_frame_sink_push_video_frame(
    xmax_external_frame_sink sink,
    const xmax_video_frame* frame
);

/**
 * Reports an unrecoverable source failure and schedules cleanup of its media and realtime connection.
 *
 * @param[in] sink The receiver whose capture or processing chain failed; stale errors are ignored.
 * @param[in] error The copied failure details with struct_size initialized and a negative code. message
 * must contain NUL-terminated UTF-8; service and HTTP codes are retained when present.
 *
 * @return XMAX_OK when the report is accepted or stale, or a negative error code for invalid arguments.
 *
 * @note This call does not wait for cleanup. Observe the failure and idle state through event polling.
 * Duplicate reports for the same binding are ignored. Your app retains ownership of its capture resources.
 */
XMAX_API xmax_result XMAX_CALL
xmax_external_frame_sink_report_error(
    xmax_external_frame_sink sink,
    const xmax_error_info* error
);

/**
 * Releases a source receiver without stopping caller-owned capture or unbinding the controller's source.
 *
 * @param[in] sink The receiver handle to release, or NULL.
 */
XMAX_API void XMAX_CALL xmax_external_frame_sink_release(
    xmax_external_frame_sink sink
);

#pragma pack(pop)
#if defined(__cplusplus)
static_assert(
    sizeof(xmax_state) == 4 && sizeof(xmax_pixel_format) == 4 && sizeof(xmax_event_type) == 4,
    "XmaxSDK requires 32-bit C enums; disable short-enum compiler flags"
);
#else
typedef char xmax_require_32bit_enums
    [(sizeof(xmax_state) == 4 && sizeof(xmax_pixel_format) == 4 && sizeof(xmax_event_type) == 4) ? 1 : -1];
#endif

/**
 * Binds caller-owned video with optional PCM and returns a source-scoped receiver.
 *
 * @param[in] realtime The controller, disconnected and either idle or media-ready.
 * @param[in] config The fixed source declaration; has_audio disables microphone capture for this source.
 * @param[out] out_sink Receives an owned receiver, or NULL on failure.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note This call blocks; use a worker thread. Capture, permissions, and pacing remain your app's responsibility.
 * Frames remain accepted for preview while disconnected. Release the receiver with xmax_external_frame_sink_release.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_bind_external_media(
    xmax_realtime realtime,
    const xmax_external_media_config* config,
    xmax_external_frame_sink* out_sink
);

/**
 * Copies, converts, and uploads source-bound PCM without playing it locally.
 *
 * @param[in] sink The source receiver created with has_audio enabled; stale receivers ignore input.
 * @param[in] frame Borrowed interleaved PCM with a timestamp on the video timeline, copied before return.
 *
 * @return XMAX_OK on success or ignored input, or a negative error code for invalid current input.
 *
 * @note PCM is converted to 48 kHz mono PCM16 in 10 ms packets and uploaded only while published.
 * No audio is queued for a future connection. A gap starts a new segment; overlapping or negative
 * timestamps are ignored. Format changes fail the binding. Audio submitted without opting in is ignored.
 * Use one serial source queue for audio submissions; do not release sink concurrently with a call.
 */
XMAX_API xmax_result XMAX_CALL xmax_external_frame_sink_push_audio_frame(
    xmax_external_frame_sink sink,
    const xmax_external_audio_frame* frame
);

/**
 * Returns the latest directional video statistics independently of event-queue consumption.
 *
 * @param[in] realtime The realtime controller.
 * @param[in] direction The transport direction: 0 for uplink or 1 for downlink.
 * @param[in,out] statistics The snapshot destination, with struct_size initialized.
 *
 * @return XMAX_OK on success, or a negative error code on failure.
 *
 * @note A snapshot with available = 0 has no current stream. Reads do not wait for lifecycle work.
 * Poll after video statistics events; disconnect clears both directions.
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_video_statistics(
    xmax_realtime realtime,
    int32_t direction,
    xmax_video_statistics* statistics
);

#ifdef __cplusplus
}
#endif
#endif
