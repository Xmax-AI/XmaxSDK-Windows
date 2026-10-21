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
 * 提供 UTF-8 文本编码的稳定 C ABI，所有结构体按八字节边界打包。
 *
 * 使用带尺寸的结构体前应设置 struct_size，创建配置需指定 XMAX_ABI_VERSION。
 * SDK 分配的资源必须交由匹配的释放接口处理，不能使用接入方分配器释放。
 * 负数结果表示失败，轮询或取帧时的 XMAX_NO_EVENT 表示队列为空。
 * C++ 异常不会跨越 C ABI；失败后应在同一线程读取 xmax_last_error。
 * 生命周期与存储操作会阻塞，应在工作线程调用；销毁句柄前必须结束并发使用。
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
 * 客户端的不透明句柄，由 xmax_client_create 创建、xmax_client_destroy 释放。
 */
typedef struct xmax_client_impl* xmax_client;

/**
 * 实时控制器的不透明句柄，由 xmax_realtime_create 创建、xmax_realtime_destroy 释放。
 */
typedef struct xmax_realtime_impl* xmax_realtime;

/**
 * 视频帧句柄，由 xmax_frame_release 释放；释放前可访问像素数据。
 */
typedef struct xmax_frame_impl* xmax_frame;

/**
 * 拥有 UTF-8 返回文本的不透明句柄，由 xmax_string_release 释放。
 */
typedef struct xmax_string_impl* xmax_string;

/**
 * 拥有摄像头枚举快照的不透明句柄，由 xmax_camera_list_release 释放。
 */
typedef struct xmax_camera_list_impl* xmax_camera_list;

/**
 * 存储传输控制器的不透明句柄，由 xmax_storage_destroy 释放。
 */
typedef struct xmax_storage_impl* xmax_storage;

/**
 * 拥有上传结果元数据的不透明句柄，由 xmax_uploaded_file_release 释放。
 */
typedef struct xmax_uploaded_file_impl* xmax_uploaded_file;

/**
 * SDK 操作结果；0 表示成功，正值表示无待处理数据，负值表示失败。
 */
typedef int32_t xmax_result;

/**
 * SDK 操作结果码，非负值不表示操作失败。
 */
enum {

    /**
     * 操作成功。
     */
    XMAX_OK = 0,

    /**
     * 当前没有可消费的事件、日志或视频帧，不表示操作失败。
     */
    XMAX_NO_EVENT = 1,

    /**
     * 参数为空、越界、格式错误或结构体版本不兼容。
     */
    XMAX_INVALID_ARGUMENT = -1,

    /**
     * 当前生命周期状态不允许执行请求操作。
     */
    XMAX_INVALID_STATE = -2,

    /**
     * 网络字节传输失败。
     */
    XMAX_NETWORK_ERROR = -3,

    /**
     * 服务端 API 返回错误或响应封装无效。
     */
    XMAX_API_ERROR = -4,

    /**
     * RTC 引擎、房间连接或原生信令操作失败。
     */
    XMAX_RTC_ERROR = -5,

    /**
     * 等待操作超过规定期限。
     */
    XMAX_TIMEOUT = -6,

    /**
     * 操作已取消或已失效。
     */
    XMAX_CANCELLED = -7,

    /**
     * 当前 SDK 构建或平台未提供请求能力。
     */
    XMAX_UNSUPPORTED = -8,

    /**
     * SDK 内部发生无法分类的失败。
     */
    XMAX_INTERNAL_ERROR = -9,

    /**
     * 提供的输出缓冲区容量不足。
     */
    XMAX_BUFFER_TOO_SMALL = -10,

    /**
     * 设备采集、像素布局或媒体处理失败。
     */
    XMAX_MEDIA_ERROR = -11,

    /**
     * 本地文件读写或目标文件发布失败。
     */
    XMAX_IO_ERROR = -12,

    /**
     * 参考图片未通过请求的安全检查。
     */
    XMAX_UNSAFE_IMAGE = -13,

    /**
     * API 凭证未配置或为空。
     */
    XMAX_INVALID_API_KEY = -14,

    /**
     * 会话数据或心跳状态无效。
     */
    XMAX_SESSION_ERROR = -15,

    /**
     * 系统拒绝摄像头访问。
     */
    XMAX_CAMERA_PERMISSION_DENIED = -16,

    /**
     * 系统拒绝麦克风访问。
     */
    XMAX_MICROPHONE_PERMISSION_DENIED = -17,

    /**
     * 文件上传失败。
     */
    XMAX_UPLOAD_ERROR = -18,

    /**
     * 文件下载失败。
     */
    XMAX_DOWNLOAD_ERROR = -19
};

/**
 * 实时媒体、连接及生成任务的生命周期状态。
 */
typedef enum xmax_state {

    /**
     * 尚未绑定可用本地媒体。
     */
    XMAX_IDLE,

    /**
     * 本地媒体已就绪，尚未建立活动 RTC 连接。
     */
    XMAX_READY,

    /**
     * 正在分配服务端会话并加入 RTC。
     */
    XMAX_CONNECTING,

    /**
     * RTC 已连接，当前未处于生成状态。
     */
    XMAX_CONNECTED,

    /**
     * 生成任务已启动并接收结果流。
     */
    XMAX_GENERATING,

    /**
     * 正在取消操作并释放连接资源。
     */
    XMAX_DISCONNECTING,

    /**
     * 正在准备本地媒体，尚不能连接或生成。
     */
    XMAX_PREPARING = 6
} xmax_state;

/**
 * 视频像素布局格式，决定平面数量和行步幅的解释方式。
 */
typedef enum xmax_pixel_format {

    /**
     * 单平面 BGRA，每像素四字节。
     */
    XMAX_BGRA = 1,
    /**
     * 双平面 4:2:0，第一平面为 Y，第二平面为交错 UV。
     */
    XMAX_NV12 = 2,
    /**
     * 三平面 4:2:0，依次为 Y、U、V。
     */
    XMAX_I420 = 3
} xmax_pixel_format;

/**
 * SDK 应用事件类别，决定事件负载中哪些字段有效。
 */
typedef enum xmax_event_type {

    /**
     * 生命周期状态变更事件。
     */
    XMAX_EVENT_STATE = 1,

    /**
     * SDK 错误事件，code 和 message 描述失败原因。
     */
    XMAX_EVENT_ERROR = 2,

    /**
     * 实际传输或视频统计事件，方向和有效字段由负载给出。
     */
    XMAX_EVENT_STATISTICS = 3,

    /**
     * 会话创建、加入、发布及结果首帧的启动耗时事件。
     */
    XMAX_EVENT_TIMING = 4,

    /**
     * 画质档位变更事件，包含方向、档位和生效规格。
     */
    XMAX_EVENT_QUALITY_CHANGE = 5, /* direction, quality=tier 1..5, effective dimensions and fps. */

    /**
     * 设备性能状态事件，code 为 1 表示受限，为 0 表示恢复。
     */
    XMAX_EVENT_PERFORMANCE = 6 /* code: 1=limited, 0=recovered; dimensions/fps are recommendations. */
} xmax_event_type;

/**
 * 可组合使用的进程级日志选项位。
 */
enum {

    /**
     * 关闭业务与性能日志。
     */
    XMAX_LOG_NONE = 0,
    /**
     * 启用生命周期、服务请求及媒体等业务诊断。
     */
    XMAX_LOG_BUSINESS = 1u << 0,
    /**
     * 启用实际帧率、处理耗时、传输统计及质量性能诊断。
     */
    XMAX_LOG_PERFORMANCE = 1u << 1,
    /**
     * 同时启用业务和性能日志。
     */
    XMAX_LOG_ALL = 3u
};

/**
 * 日志严重程度。
 */
typedef enum xmax_log_level {

    /**
     * 调试级详细诊断。
     */
    XMAX_LOG_DEBUG,

    /**
     * 正常操作与状态信息。
     */
    XMAX_LOG_INFO,

    /**
     * 需要关注但不一定导致操作失败的状态。
     */
    XMAX_LOG_WARNING,

    /**
     * 操作失败或不可继续的错误。
     */
    XMAX_LOG_ERROR
} xmax_log_level;

/**
 * 日志消息的输出语言。
 */
typedef enum xmax_log_language {

    /**
     * 使用中文日志消息。
     */
    XMAX_LOG_CHINESE,
    /**
     * 使用英文日志消息。
     */
    XMAX_LOG_ENGLISH
} xmax_log_language;

/**
 * 保存一条经过脱敏的日志记录；文本缓冲区由结构体自身拥有。
 */
typedef struct xmax_log_record {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 本记录的严重程度，使用 XMAX_LOG_* 级别枚举。
     */
    xmax_log_level level;

    /**
     * 业务与性能日志选项位掩码。
     */
    uint32_t options;

    /**
     * 保留字段，接入方应保持为零。
     */
    uint32_t reserved;

    /**
     * 记录生成时的 Unix UTC 微秒时间戳。
     */
    int64_t timestamp_us;

    /**
     * 进程内单调递增的记录序号；丢弃记录时可能不连续。
     */
    uint64_t sequence;

    /**
     * 以 NUL 结尾的日志分类文本。
     */
    char category[32];

    /**
     * 以 NUL 结尾的 UTF-8 诊断文本。
     */
    char message[4096];

} xmax_log_record;

/**
 * 指定客户端 ABI 版本、API 凭证、模型及请求超时；创建时复制字符串。
 */
typedef struct xmax_client_config {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * SDK ABI 版本，创建配置时应使用 XMAX_ABI_VERSION。
     */
    uint32_t abi_version;

    /**
     * UTF-8 API 凭证；创建客户端时复制，可为空以进行本地准备。
     */
    const char* api_key;

    /**
     * 实时生成模型标识，当前仅支持 x2.1；NULL 使用默认模型。
     */
    const char* model;

    /**
     * HTTP 请求超时，单位为毫秒；0 使用默认的 15000 毫秒。
     */
    uint32_t http_timeout_ms;

} xmax_client_config;

/**
 * 视频尺寸和帧率，可表示外部输入规格或摄像头的 RTC 编码目标。
 *
 * 摄像头编码目标支持 1920×1080、1080×1920、1408×1408、1152×1536、1536×1152，帧率范围为 [1, 60]。
 * 外部输入支持 [4, 8192] 的偶数宽高，总像素不超过 2100000，不限于摄像头预设。
 * 兼容旧版 1920×1024 和 1024×1920 输入；摄像头的旧尺寸分别映射为 1920×1080 和 1080×1920。
 * 摄像头保留设备实际输出的帧尺寸，由 RTC 按编码目标处理比例和分辨率。
 */
typedef struct xmax_video_format {

    /**
     * 视频宽度，单位为像素。
     */
    uint32_t width;

    /**
     * 视频高度，单位为像素。
     */
    uint32_t height;

    /**
     * 每秒视频帧数。
     */
    uint32_t fps;

} xmax_video_format;

/**
 * 描述只读视频平面视图，包含像素格式、单调时间戳、字节步幅和可访问长度。
 */
typedef struct xmax_video_frame {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 视频像素格式，决定平面数量及各平面的字节布局。
     */
    xmax_pixel_format pixel_format;

    /**
     * 视频宽度，单位为像素。
     */
    uint32_t width;

    /**
     * 视频高度，单位为像素。
     */
    uint32_t height;

    /**
     * 严格递增的单调微秒时间戳，用于同一输入绑定内的帧排序。
     */
    int64_t timestamp_us;

    /**
     * 借用的只读平面指针，像素有效期由输入调用或拥有该视图的帧句柄限定。
     */
    const uint8_t* planes[3];

    /**
     * 各平面的行步幅，单位为字节，不是像素数。
     */
    size_t strides[3];

    /**
     * 从各平面指针起可访问的字节数，用于校验行布局。
     */
    size_t plane_sizes[3];

} xmax_video_frame;

/**
 * 指定生成提示词、可选参考图片及人脸修复条件；调用期间字符串必须有效。
 */
typedef struct xmax_generation_context {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * UTF-8 生成提示词。
     */
    const char* prompt;

    /**
     * 可选的参考图片路径或服务可接受的地址；NULL 表示不使用参考图片。
     */
    const char* reference_path;

    /**
     * 人脸修复开关，非零表示启用。
     */
    int32_t face_restoration;

} xmax_generation_context;

/**
 * 保存生命周期、错误、质量或统计事件；负数统计值表示数据不可用。
 */
typedef struct xmax_event {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 事件类别，决定其余字段中哪些数据有效。
     */
    xmax_event_type type;

    /**
     * 事件发生时或状态快照中的实时生命周期状态。
     */
    xmax_state state;

    /**
     * SDK 错误码或性能告警标记，具体含义由事件类别决定。
     */
    xmax_result code;

    /**
     * 以 NUL 结尾的 UTF-8 诊断文本。
     */
    char message[512];

    /**
     * 以 NUL 结尾的服务端会话标识，尚未创建时为空。
     */
    char session_id[128];

    /**
     * 以 NUL 结尾的生成任务标识，尚未启动时为空。
     */
    char task_id[128];

    /**
     * 传输方向：0 表示上行，1 表示下行。
     */
    int32_t direction;

    /**
     * 事件对应视频的像素宽度。
     */
    int32_t width;

    /**
     * 事件对应视频的像素高度。
     */
    int32_t height;

    /**
     * 事件对应视频的每秒帧数。
     */
    int32_t fps;

    /**
     * 实际视频码率，单位为 kbps。
     */
    int32_t bitrate_kbps;

    /**
     * 视频丢包率，单位为百分比。
     */
    int32_t packet_loss_percent;

    /**
     * 网络质量或画质档位，具体含义由事件类型决定。
     */
    int32_t quality;

    /**
     * 端到端延迟，单位为毫秒。
     */
    int32_t e2e_ms;

    /**
     * 创建服务端会话的耗时，单位为毫秒。
     */
    double session_ms;

    /**
     * 加入 RTC 房间的耗时，单位为毫秒。
     */
    double rtc_join_ms;

    /**
     * 本地视频首次实际发布的耗时，单位为毫秒。
     */
    double publish_ms;

    /**
     * 开始生成到结果首帧的耗时，单位为毫秒。
     */
    double first_frame_ms;

    /**
     * 会话启动到结果首帧的总耗时，单位为毫秒。
     */
    double total_ms;

} xmax_event;

/**
 * SDK 能力位；只有已实现能力才会出现在查询结果中。
 */
enum {

    /**
     * 当前构建提供原生 RTC 连接与视频传输。
     */
    XMAX_CAP_RTC = 1u << 0,

    /**
     * 当前构建提供原生摄像头采集。
     */
    XMAX_CAP_CAMERA = 1u << 1,

    /**
     * 当前构建提供原生 HWND 视频呈现。
     */
    XMAX_CAP_NATIVE_WINDOW = 1u << 2,

    /**
     * 当前构建提供文件上传、下载与存储操作。
     */
    XMAX_CAP_STORAGE = 1u << 3,

    /**
     * 当前构建提供原生视频帧插值后端。
     */
    XMAX_CAP_FRAME_INTERPOLATION = 1u << 4,

    /**
     * 提供轨迹坐标映射、当前任务信令发送和效果数据查询。
     */
    XMAX_CAP_TRAJECTORY = 1u << 5
};

/**
 * 原生窗口呈现时的媒体比例与裁剪策略。
 */
enum {

    /**
     * 保持视频比例完整显示，空余区域补边。
     */
    XMAX_CONTENT_FIT = 0,
    /**
     * 保持视频比例填满目标区域，超出区域居中裁剪。
     */
    XMAX_CONTENT_FILL = 1
};

/**
 * 返回当前 SDK 的 ABI 版本与已实现能力位。
 */
typedef struct xmax_capabilities {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * SDK ABI 版本，创建配置时应使用 XMAX_ABI_VERSION。
     */
    uint32_t abi_version;

    /**
     * 当前 SDK 已实现的能力位掩码，由 XMAX_CAP_* 常量解释。
     */
    uint64_t flags;

} xmax_capabilities;

/**
 * 借用摄像头枚举快照中的设备字符串，快照释放后不得继续访问。
 */
typedef struct xmax_camera_info {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 摄像头设备标识，用于后续明确选择该设备。
     */
    const char* id;

    /**
     * 用于界面显示的摄像头名称，不用于唯一匹配设备。
     */
    const char* name;

    /**
     * 非零表示本次枚举结果中的默认摄像头，0 表示其他设备。
     */
    int32_t is_default;

} xmax_camera_info;

/**
 * 配置摄像头设备、RTC 编码目标和连接时的麦克风策略。
 */
typedef struct xmax_camera_config {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * UTF-8 摄像头标识；NULL 或空文本选择默认设备。
     */
    const char* device_id;

    /**
     * RTC 编码目标；宽、高、帧率全部为 0 时使用 1920×1080、30 fps，实际采集尺寸以帧视图为准。
     */
    xmax_video_format format;

    /**
     * 连接 RTC 时的麦克风开关，非零表示启用。
     */
    int32_t use_microphone;

} xmax_camera_config;

/**
 * 存储传输的生命周期状态。
 */
enum {

    /**
     * 当前没有执行传输。
     */
    XMAX_TRANSFER_IDLE,

    /**
     * 传输正在进行。
     */
    XMAX_TRANSFER_RUNNING,

    /**
     * 传输已成功完成。
     */
    XMAX_TRANSFER_SUCCEEDED,

    /**
     * 传输因错误终止。
     */
    XMAX_TRANSFER_FAILED,

    /**
     * 传输因取消请求终止。
     */
    XMAX_TRANSFER_CANCELLED
};

/**
 * 传输进度，包含传输状态、已完成字节数和总字节数。
 */
typedef struct xmax_transfer_progress {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 当前传输状态，使用 XMAX_TRANSFER_* 常量。
     */
    int32_t state;

    /**
     * 已传输的字节数。
     */
    uint64_t transferred;

    /**
     * 传输总字节数；无法确定总大小时为 0。
     */
    uint64_t total;

} xmax_transfer_progress;

/**
 * 指定上传文件名、可选 MIME 类型及图片安全检查策略。
 */
typedef struct xmax_upload_options {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 上传对象文件名；文件上传时可省略，字节上传时必须提供。
     */
    const char* file_name;

    /**
     * 可选 MIME 类型；NULL 或空文本根据文件扩展名推断。
     */
    const char* content_type;

    /**
     * 图片安全检查开关，非零表示对图片上传请求执行检查。
     */
    int32_t check_image_safety;

} xmax_upload_options;

/**
 * 借用上传结果中的访问地址、对象键和 ETag，结果句柄释放后不得访问。
 */
typedef struct xmax_uploaded_file_info {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 上传成功后的文件访问地址。
     */
    const char* url;

    /**
     * 存储服务返回的对象键，用于定位已上传对象。
     */
    const char* object_key;

    /**
     * 上传对象的借用 ETag 文本；服务未返回该值时为 NULL。
     */
    const char* etag;

} xmax_uploaded_file_info;

/**
 * 带宽或性能受限时的编码降级偏好。
 */
enum {

    /**
     * 由原生编码器决定降级偏好。
     */
    XMAX_ENCODER_AUTO = 0,
    /**
     * 带宽或性能受限时优先保持帧率。
     */
    XMAX_ENCODER_MAINTAIN_FRAMERATE = 1,
    /**
     * 带宽或性能受限时优先保持画质。
     */
    XMAX_ENCODER_MAINTAIN_QUALITY = 2
};

/**
 * 指定上行编码码率限制及画质与帧率的降级偏好。
 */
typedef struct xmax_encoding_config {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 最小码率，单位为 kbps；-1 使用策略默认值，0 不设置最小限制。
     */
    int32_t minimum_bitrate_kbps;

    /**
     * 最大码率，单位为 kbps；0 使用当前规格的策略估计值。
     */
    int32_t maximum_bitrate_kbps;

    /**
     * 编码降级偏好，使用 XMAX_ENCODER_* 常量。
     */
    int32_t preference;

} xmax_encoding_config;

/**
 * 返回自适应开关、双向画质档位及各方向当前生效的视频规格。
 */
typedef struct xmax_quality_status {

    /**
     * 本结构体的字节数；调用前设置为对应结构体的 sizeof 值。
     */
    uint32_t struct_size;

    /**
     * 双向画质自适应开关，非零表示启用。
     */
    int32_t adaptive_enabled;

    /**
     * 当前上行画质档位，范围为 [1, 5]。
     */
    int32_t uplink_level;

    /**
     * 当前下行画质档位，范围为 [1, 5]。
     */
    int32_t downlink_level;

    /**
     * 上行当前生效的视频尺寸与帧率。
     */
    xmax_video_format uplink;

    /**
     * 下行当前生效的视频尺寸与帧率。
     */
    xmax_video_format downlink;

} xmax_quality_status;

/**
 * SDK 服务环境，独立于实时生成模型。
 */
typedef enum xmax_environment {

    /**
     * 国内服务环境。
     */
    XMAX_ENVIRONMENT_CHINA = 0,

    /**
     * 海外服务环境。
     */
    XMAX_ENVIRONMENT_GLOBAL = 1

} xmax_environment;

/**
 * 摄像头编码分辨率预设，实际采集帧可使用设备原生尺寸。
 */
typedef enum xmax_camera_preset {

    /**
     * 没有摄像头流或没有匹配的预设。
     */
    XMAX_CAMERA_PRESET_NONE = 0,

    /**
     * 16:9 横向画面。
     */
    XMAX_CAMERA_PRESET_1920_1080 = 1,

    /**
     * 9:16 竖向画面。
     */
    XMAX_CAMERA_PRESET_1080_1920 = 2,

    /**
     * 1:1 正方形画面。
     */
    XMAX_CAMERA_PRESET_1408_1408 = 3,

    /**
     * 3:4 竖向画面。
     */
    XMAX_CAMERA_PRESET_1152_1536 = 4,

    /**
     * 4:3 横向画面。
     */
    XMAX_CAMERA_PRESET_1536_1152 = 5

} xmax_camera_preset;

/**
 * 实时状态变化原因；正常操作开始时清除此前失败原因。
 */
typedef enum xmax_realtime_reason {

    /**
     * 正常操作或主动停止。
     */
    XMAX_REASON_NORMAL = 0,

    /**
     * 显示方向发生变化，需要重新配置。
     */
    XMAX_REASON_ORIENTATION_CHANGED = 1,

    /**
     * 操作或运行时错误，详情见错误信息。
     */
    XMAX_REASON_FAILURE = 2

} xmax_realtime_reason;

/**
 * 实时生成模型能力；字符串为 SDK 静态文本，不得释放。
 */
typedef struct xmax_model_info {

    /**
     * 调用前设为 sizeof(xmax_model_info)。
     */
    uint32_t struct_size;

    /**
     * 服务端接受的模型标识。
     */
    const char* identifier;

    /**
     * 模型展示名称。
     */
    const char* display_name;

    /**
     * 支持的五组视频尺寸，帧率为推荐默认值。
     */
    xmax_video_format supported_formats[5];

    /**
     * supported_formats 中的有效元素数量。
     */
    uint32_t format_count;

    /**
     * 无固定尺寸列表时使用的最小生成画布像素数，不要求放大输入帧。
     */
    uint32_t minimum_input_pixels;

    /**
     * 本地视频帧的最大总像素数。
     */
    uint32_t maximum_input_pixels;

    /**
     * 无固定尺寸列表时生成画布宽高的对齐倍数。
     */
    uint32_t input_size_alignment;

    /**
     * 默认每秒视频帧数。
     */
    uint32_t default_frame_rate;

    /**
     * 摄像头默认编码目标。
     */
    xmax_video_format default_camera_format;

} xmax_model_info;

/**
 * 创建客户端的全局配置，模型由实时控制器单独选择。
 */
typedef struct xmax_client_options {

    /**
     * 调用前设为 sizeof(xmax_client_options)。
     */
    uint32_t struct_size;

    /**
     * 使用 XMAX_ABI_VERSION。
     */
    uint32_t abi_version;

    /**
     * 创建时复制的 UTF-8 API 凭证；为空时仅允许本地准备。
     */
    const char* api_key;

    /**
     * 通用 API 与实时会话使用的服务环境。
     */
    xmax_environment environment;

    /**
     * 日志位掩码，0 关闭；输出语言随环境设置，影响进程内所有客户端。
     */
    uint32_t logger_options;

    /**
     * HTTP 请求超时，单位为毫秒；0 使用 15000。
     */
    uint32_t http_timeout_ms;

} xmax_client_options;

/**
 * 实时业务配置；Windows 不启用远端视频插帧。
 */
typedef struct xmax_realtime_config {

    /**
     * 调用前设为 sizeof(xmax_realtime_config)。
     */
    uint32_t struct_size;

    /**
     * 创建时复制的模型标识；NULL 使用 x2.1。
     */
    const char* model;

} xmax_realtime_config;

/**
 * SDK 错误详情；api_code 与 http_status 为 0 时表示未提供。
 */
typedef struct xmax_error_info {

    /**
     * 调用前设为 sizeof(xmax_error_info)。
     */
    uint32_t struct_size;

    /**
     * SDK 统一错误码。
     */
    xmax_result code;

    /**
     * 服务端业务错误码。
     */
    int32_t api_code;

    /**
     * HTTP 响应状态码。
     */
    int32_t http_status;

    /**
     * 经过脱敏、以 NUL 结尾的 UTF-8 错误说明。
     */
    char message[1024];

} xmax_error_info;

/**
 * 包含状态原因和错误详情的事件，与旧事件接口消费同一个队列。
 */
typedef struct xmax_event_info {

    /**
     * 调用前设为 sizeof(xmax_event_info)。
     */
    uint32_t struct_size;

    /**
     * 原有状态、统计或诊断事件。
     */
    xmax_event event;

    /**
     * 状态或错误事件的原因，其他事件为正常值。
     */
    xmax_realtime_reason reason;

    /**
     * 状态关联错误或错误事件的详情。
     */
    xmax_error_info error;

} xmax_event_info;

/**
 * 二维坐标；输入采样使用视口坐标，输出轨迹使用 [0, 1] 归一化坐标。
 */
typedef struct xmax_point {

    /**
     * 横向坐标，向右增大。
     */
    double x;

    /**
     * 纵向坐标，向下增大。
     */
    double y;

} xmax_point;

/**
 * 一次轨迹采样；尺寸与采样点须使用同一视口坐标单位。
 */
typedef struct xmax_interaction_frame {

    /**
     * 调用前设为 sizeof(xmax_interaction_frame)。
     */
    uint32_t struct_size;

    /**
     * 按采样顺序排列的坐标，仅在提交调用期间借用。
     */
    const xmax_point* points;

    /**
     * 采样点数量，范围为 [1, 256]。
     */
    size_t point_count;

    /**
     * 视口宽度，必须为有限正数。
     */
    double viewport_width;

    /**
     * 视口高度，必须为有限正数。
     */
    double viewport_height;

    /**
     * 当前显示视频的像素宽度，须与实际显示帧一致。
     */
    uint32_t displayed_video_width;

    /**
     * 当前显示视频的像素高度。
     */
    uint32_t displayed_video_height;

    /**
     * 画面布局方式，使用 XMAX_CONTENT_FIT 或 XMAX_CONTENT_FILL。
     */
    int32_t content_mode;

} xmax_interaction_frame;

/**
 * 查询模型支持的规格、像素限制和默认配置。
 *
 * @param[in] model 模型标识；NULL 使用 x2.1。
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL xmax_model_get_info(
    const char* model,
    xmax_model_info* info
);

/**
 * 复制全局配置并创建客户端，不启动采集或云端会话。
 *
 * @param[in] options 全局配置，需指定 ABI 版本和服务环境。
 * @param[out] out_client 接收客户端；失败时置为 NULL，由 xmax_client_destroy 释放。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL
xmax_client_create_with_options(
    const xmax_client_options* options,
    xmax_client* out_client
);

/**
 * 使用独立模型配置创建实时控制器。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[in] config 实时配置，创建时复制。
 * @param[out] out_realtime 接收实时句柄；失败时置为 NULL，由 xmax_realtime_destroy 释放。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_create_with_config(
    xmax_client client,
    const xmax_realtime_config* config,
    xmax_realtime* out_realtime
);

/**
 * 读取当前线程最近一次操作的结构化错误；本查询不会清除错误。
 *
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL xmax_last_error_info(
    xmax_error_info* info
);

/**
 * 读取当前状态、状态原因和关联错误。
 *
 * @param[in] realtime 实时句柄。
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_state_info(
    xmax_realtime realtime,
    xmax_event_info* info
);

/**
 * 非阻塞读取包含状态原因和错误详情的最早事件。
 *
 * @param[in] realtime 实时句柄。
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 队列为空返回 XMAX_NO_EVENT；与 xmax_realtime_poll_event 共用队列，请选择一种消费方式。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_poll_event_info(
    xmax_realtime realtime,
    xmax_event_info* info
);

/**
 * 按业务原因断开连接，保留本地媒体预览。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] reason 正常停止或方向变化；失败原因由 SDK 报告。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_disconnect_with_reason(
    xmax_realtime realtime,
    xmax_realtime_reason reason
);

/**
 * 更新已就绪且未连接的摄像头编码规格，保留预览绑定和设备选择。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] format 模型支持的新规格，保留已设置的码率和编码偏好。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 操作会阻塞，应在工作线程执行；准备失败时尝试恢复此前规格。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_update_camera_format(
    xmax_realtime realtime,
    const xmax_video_format* format
);

/**
 * 切换已就绪摄像头的分辨率，保留帧率和编码配置。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] preset 目标分辨率预设，不能为 NONE。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_update_camera_preset(
    xmax_realtime realtime,
    xmax_camera_preset preset
);

/**
 * 查询摄像头当前编码规格与预设，不随网络调档改变。
 *
 * @param[in] realtime 实时句柄。
 * @param[out] format 接收规格；没有摄像头流时清零。
 * @param[out] preset 接收预设；没有摄像头流时为 NONE。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_camera_format(
    xmax_realtime realtime,
    xmax_video_format* format,
    xmax_camera_preset* preset
);

/**
 * 在生成中设置上行档位，后续网络自适应仍可继续调档。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] level 目标档位，范围为 [1, 5]；1 画质最高。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 操作会阻塞；失败时保留此前档位。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_uplink_quality_level(
    xmax_realtime realtime,
    int32_t level
);

/**
 * 在生成中设置下行档位，后续网络自适应仍可继续调档。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] level 目标档位，范围为 [1, 5]；1 画质最高。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 操作会阻塞；失败时保留此前档位。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_set_downlink_quality_level(
    xmax_realtime realtime,
    int32_t level
);

/**
 * 提交轨迹采样，由 SDK 映射视频坐标并异步发送当前任务的 tracks 信令。
 *
 * @param[in] realtime 正在生成的实时句柄。
 * @param[in] frame 当前视口的轨迹采样，调用时同步复制。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 采样采用最新值合并，适应模式的黑边采样会被忽略；成功表示已接收，不表示服务端已处理。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_submit_interaction(
    xmax_realtime realtime,
    const xmax_interaction_frame* frame
);

/**
 * 复制当前任务尚未过期的归一化轨迹，供界面绘制自定义效果。
 *
 * @param[in] realtime 实时句柄。
 * @param[out] points 输出点数组；capacity 为 0 时允许为 NULL。
 * @param[in] capacity 数组容量，最多需要 256 个元素。
 * @param[out] count 接收所需点数；容量不足返回 XMAX_BUFFER_TOO_SMALL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_get_trajectory(
    xmax_realtime realtime,
    xmax_point* points,
    size_t capacity,
    size_t* count
);

/**
 * 在 HWND 上显示指定结果帧，并叠加默认轨迹效果。
 *
 * @param[in] realtime 实时句柄；轨迹沿用提交采样的任务。
 * @param[in] frame 待显示帧，调用期间必须有效。
 * @param[in] window Windows HWND，由当前线程拥有。
 * @param[in] content_mode 适应或填满模式，应与轨迹采样配置一致。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 从 HWND 所属线程调用；其他界面框架可通过轨迹查询接口自行绘制。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_render_window(
    xmax_realtime realtime,
    xmax_frame frame,
    void* window,
    int32_t content_mode
);

/**
 * 获取 SDK 的语义版本号。
 *
 * @return 静态 UTF-8 版本文本，不得修改或释放。
 */
XMAX_API const char* XMAX_CALL xmax_version(
    void
);

/**
 * 获取当前线程最近一次 SDK 操作的诊断文本。
 *
 * @return 借用的 NUL 结尾 UTF-8 文本，不得释放；需保留时应立即复制。
 * @note 诊断在当前线程下一次 SDK 操作前有效；不同线程的错误文本相互独立。
 */
XMAX_API const char* XMAX_CALL xmax_last_error(
    void
);

/**
 * 配置当前进程所有客户端共享的日志选项与语言。
 *
 * @param[in] options 日志位掩码，可组合 XMAX_LOG_BUSINESS 和 XMAX_LOG_PERFORMANCE；XMAX_LOG_NONE 表示关闭。
 * @param[in] language 后续记录使用的中文或英文输出语言。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 线程安全。日志默认关闭，每次配置变更会清空待消费记录。
 */
XMAX_API xmax_result XMAX_CALL xmax_logger_configure(
    uint32_t options,
    xmax_log_language language
);

/**
 * 读取当前进程共享的日志配置。
 *
 * @param[out] options 接收已启用日志位掩码的输出指针，不得为 NULL。
 * @param[out] language 接收输出语言的指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 线程安全，创建客户端不会重置该配置。
 */
XMAX_API xmax_result XMAX_CALL xmax_logger_get_config(
    uint32_t* options,
    xmax_log_language* language
);

/**
 * 非阻塞取出进程级日志队列中最早的记录。
 *
 * @param[in,out] record 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 * @note 进程级日志队列应由单一消费者读取；最多保留最新的 256 条记录。
 */
XMAX_API xmax_result XMAX_CALL xmax_logger_poll(
    xmax_log_record* record
);

/**
 * 复制配置并创建客户端，不启动摄像头或云端会话。
 *
 * @param[in] config 客户端配置，需设置 struct_size 和 XMAX_ABI_VERSION；空 API 凭证允许进行本地准备。
 * @param[out] out_client 接收客户端句柄的输出指针，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回句柄由 xmax_client_destroy 释放；输入字符串在创建时复制。
 */
XMAX_API xmax_result XMAX_CALL xmax_client_create(
    const xmax_client_config* config,
    xmax_client* out_client
);

/**
 * 释放客户端句柄及其独占资源。
 *
 * @param[in] client 待释放句柄，可为 NULL。
 * @note 释放前应结束使用该句柄的并发调用；已创建的子控制器独立保留共享配置。
 */
XMAX_API void XMAX_CALL xmax_client_destroy(
    xmax_client client
);

/**
 * 查询当前 SDK 构建是否包含 RTC 后端。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] available 接收可用标记的输出指针，不得为 NULL；1 表示可用，0 表示不可用。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
XMAX_API xmax_result XMAX_CALL xmax_client_has_rtc(
    xmax_client client,
    int32_t* available
);

/**
 * 查询当前 SDK 构建的能力位与 ABI 版本。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[in,out] capabilities 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
XMAX_API xmax_result XMAX_CALL
xmax_client_get_capabilities(
    xmax_client client,
    xmax_capabilities* capabilities
);

/**
 * 枚举摄像头设备，不启动采集或云端会话。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out_list 接收枚举快照句柄的输出指针，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 快照由 xmax_camera_list_release 释放。
 */
XMAX_API xmax_result XMAX_CALL xmax_client_list_cameras(
    xmax_client client,
    xmax_camera_list* out_list
);

/**
 * 获取摄像头枚举快照中的设备数量。
 *
 * @param[in] list 枚举快照句柄，可为 NULL。
 * @return 设备数量；句柄为 NULL 时返回 0。
 */
XMAX_API size_t XMAX_CALL xmax_camera_list_count(
    xmax_camera_list list
);

/**
 * 读取枚举快照中指定摄像头的信息。
 *
 * @param[in] list 有效枚举快照句柄，不得为 NULL。
 * @param[in] index 从 0 开始的设备索引，必须小于设备数量。
 * @param[in,out] info 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回的设备字符串由枚举快照拥有，在快照释放前有效。
 */
XMAX_API xmax_result XMAX_CALL
xmax_camera_list_get(
    xmax_camera_list list,
    size_t index,
    xmax_camera_info* info
);

/**
 * 释放摄像头枚举快照及其设备字符串。
 *
 * @param[in] list 待释放的快照句柄，可为 NULL。
 */
XMAX_API void XMAX_CALL xmax_camera_list_release(
    xmax_camera_list list
);

/**
 * 创建独立的实时控制器，不建立云端连接。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out_realtime 接收实时控制器句柄的输出指针，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回句柄由 xmax_realtime_destroy 释放，并独立保留客户端服务配置。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_create(
    xmax_client client,
    xmax_realtime* out_realtime
);

/**
 * 关闭并释放实时控制器的会话、RTC 和媒体资源。
 *
 * @param[in] realtime 待释放的实时控制器句柄，可为 NULL。
 * @note 释放前应结束使用该句柄的并发调用；该操作可能等待资源清理。
 */
XMAX_API void XMAX_CALL xmax_realtime_destroy(
    xmax_realtime realtime
);

/**
 * 绑定接入方持续提供视频帧的外部输入。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。绑定外部输入会停止此前的本地媒体；需在连接前调用。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_prepare_external(
    xmax_realtime realtime,
    const xmax_video_format* format
);

/**
 * 启动 SDK 摄像头采集，并等待第一张真实视频帧。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] config 带 struct_size 的摄像头配置；空设备标识使用默认摄像头，视频规格全部为 0 时使用
 * 当前模型的摄像头默认规格。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。仅准备本地视频，不创建服务端会话；麦克风在后续连接时按配置启动。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_prepare_camera(
    xmax_realtime realtime,
    const xmax_camera_config* config
);

/**
 * 切换活动摄像头，保留连接并恢复已有生成条件。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] device_id UTF-8 设备标识；空文本使用默认摄像头，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。等待新摄像头就绪；失败时清理连接与本地媒体并返回错误。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_switch_camera(
    xmax_realtime realtime,
    const char* device_id
);

/**
 * 释放断开连接后的本地采集和输入绑定。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。存在活动连接时应先断开连接。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_stop_media(
    xmax_realtime realtime
);

/**
 * 创建服务端会话并加入 RTC，暂不启动生成。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] use_microphone 麦克风开关，非零表示加入房间时启用。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。调用前必须准备本地媒体；摄像头配置中的麦克风选项同样生效。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_connect(
    xmax_realtime realtime,
    int32_t use_microphone
);

/**
 * 使用指定或缓存条件启动生成，并等待结果首帧。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] context 生成条件，字符串在本次同步调用返回前必须有效。可为 NULL 以复用条件，但首次生成必须提供。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。存在就绪媒体但尚未连接时会先连接；外部输入由其他线程持续供帧。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_generate(
    xmax_realtime realtime,
    const xmax_generation_context* context
);

/**
 * 读取当前状态和会话标识快照，不消费待处理事件。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in,out] state 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此查询可能等待串行生命周期队列。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_state(
    xmax_realtime realtime,
    xmax_event* state
);

/**
 * 读取外部输入规格或摄像头的 RTC 编码目标，实际采集尺寸以本地帧视图为准。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[out] format 接收输入规格的输出指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此查询可能等待串行生命周期队列。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_format(
    xmax_realtime realtime,
    xmax_video_format* format
);

/**
 * 读取当前或下次连接使用的远端播放音量。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[out] volume 接收 [0, 1] 范围音量的输出指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 初始音量为 0；此查询可能等待串行生命周期队列。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_get_remote_volume(
    xmax_realtime realtime,
    float* volume
);

/**
 * 配置上行码率限制与编码降级偏好。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] config 带 struct_size 的编码配置；最小码率 -1 使用策略默认值，最大码率 0 使用策略估计值。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。已连接时更新当前原生编码器。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_set_encoding(
    xmax_realtime realtime,
    const xmax_encoding_config* config
);

/**
 * 开启或关闭独立的双向画质自适应。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] enabled 非零启用，0 关闭；变更时重置网络质量观察窗口。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_adaptive_quality(
    xmax_realtime realtime,
    int32_t enabled
);

/**
 * 读取当前双向画质档位和生效规格。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in,out] quality 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此查询可能等待串行生命周期队列。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_get_quality(
    xmax_realtime realtime,
    xmax_quality_status* quality
);

/**
 * 请求开启或关闭帧插值。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] enabled 非零表示请求启用帧插值。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 缺少原生插值后端时，启用请求返回 XMAX_UNSUPPORTED；关闭请求仍可成功。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_interpolation(
    xmax_realtime realtime,
    int32_t enabled
);

/**
 * 使用初始条件连接并启动生成，等待结果首帧。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] context 生成条件，字符串在本次同步调用返回前必须有效。不得为 NULL。
 * @param[in] use_microphone 麦克风开关，非零表示连接时启用。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。调用前必须准备媒体；外部视频输入必须由其他线程持续提交。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_start(
    xmax_realtime realtime,
    const xmax_generation_context* context,
    int32_t use_microphone
);

/**
 * 替换正在运行的生成任务条件。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] context 生成条件，字符串在本次同步调用返回前必须有效。不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。要求当前已处于生成状态。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_update(
    xmax_realtime realtime,
    const xmax_generation_context* context
);

/**
 * 断开服务端与 RTC 连接，保留本地预览和缓存条件。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。同时请求取消在途生命周期操作。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_disconnect(
    xmax_realtime realtime
);

/**
 * 断开连接并释放本地媒体和缓存生成条件。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。控制器句柄仍有效，可再次准备媒体并启动新会话。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_close(
    xmax_realtime realtime
);

/**
 * 查询当前是否需要接入方提供外部视频帧。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[out] requesting 接收请求标记的输出指针，不得为 NULL；摄像头输入模式始终为 0。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 该标记不承担外部源帧率控制；查询不等待生命周期操作。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_is_requesting_frames(
    xmax_realtime realtime,
    int32_t* requesting
);

/**
 * 同步复制并提交一帧外部输入视频。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] frame 带 struct_size 的有效视频视图，尺寸须匹配绑定规格，时间戳须严格递增。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回后可复用原像素内存。绑定 SDK 摄像头时不能提交外部帧；未连接时也可用于本地预览。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_push_video_frame(
    xmax_realtime realtime,
    const xmax_video_frame* frame
);

/**
 * 设置当前或下次连接使用的远端播放音量。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in] volume 范围为 [0, 1] 的有限数值，0 表示静音。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_set_remote_volume(
    xmax_realtime realtime,
    float volume
);

/**
 * 非阻塞消费最早的待处理应用事件。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[in,out] out_event 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_poll_event(
    xmax_realtime realtime,
    xmax_event* out_event
);

/**
 * 非阻塞取出最新远端结果帧。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[out] out_frame 接收视频帧句柄的输出指针，不得为 NULL；像素在句柄释放前有效，无帧或失败时输出句柄置为
 * NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 * @note 返回帧由 xmax_frame_release 释放；新帧替换尚未消费的旧帧。
 */
XMAX_API xmax_result XMAX_CALL xmax_realtime_acquire_frame(
    xmax_realtime realtime,
    xmax_frame* out_frame
);

/**
 * 非阻塞取出最新本地预览帧。
 *
 * @param[in] realtime 实时控制器句柄，不得为 NULL。
 * @param[out] out_frame 接收视频帧句柄的输出指针，不得为 NULL；像素在句柄释放前有效，无帧或失败时输出句柄置为
 * NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 * @note 返回帧由 xmax_frame_release 释放；取帧不会等待媒体采集。
 */
XMAX_API xmax_result XMAX_CALL
xmax_realtime_acquire_local_frame(
    xmax_realtime realtime,
    xmax_frame* out_frame
);

/**
 * 借用帧的只读像素布局。
 *
 * @param[in] frame 有效帧句柄，不得为 NULL。
 * @param[in,out] out_view 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 视图不持有额外像素所有权；释放帧句柄后不得继续访问视图。
 */
XMAX_API xmax_result XMAX_CALL xmax_frame_get_view(
    xmax_frame frame,
    xmax_video_frame* out_view
);

/**
 * 释放帧句柄及其持有的像素生命周期。
 *
 * @param[in] frame 待释放帧句柄，可为 NULL。
 */
XMAX_API void XMAX_CALL xmax_frame_release(
    xmax_frame frame
);

/**
 * 在目标 HWND 所属线程呈现一帧视频。
 *
 * @param[in] frame 有效帧句柄，不得为 NULL。
 * @param[in] hwnd 目标 Windows HWND 转换为 void* 后的值。
 * @param[in] content_mode XMAX_CONTENT_FIT 等比例适应，或 XMAX_CONTENT_FILL 居中填满。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 必须从 HWND 所属线程调用；不支持原生窗口的平台返回 XMAX_UNSUPPORTED。
 */
XMAX_API xmax_result XMAX_CALL xmax_frame_render_window(
    xmax_frame frame,
    void* hwnd,
    int32_t content_mode
);

/**
 * 创建独立的存储传输控制器。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out_storage 接收存储控制器句柄，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回句柄由 xmax_storage_destroy 释放，并独立保留客户端配置。
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_create(
    xmax_client client,
    xmax_storage* out_storage
);

/**
 * 释放存储传输控制器。
 *
 * @param[in] storage 待释放的存储句柄，可为 NULL。
 * @note 释放前应取消并等待在途传输退出，结束使用句柄的并发调用。
 */
XMAX_API void XMAX_CALL xmax_storage_destroy(
    xmax_storage storage
);

/**
 * 请求取消当前存储传输。
 *
 * @param[in] storage 存储控制器句柄，可为 NULL。
 * @note 允许从其他线程调用，不等待传输退出。
 */
XMAX_API void XMAX_CALL xmax_storage_cancel(
    xmax_storage storage
);

/**
 * 线程安全地读取当前传输状态和字节进度。
 *
 * @param[in] storage 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in,out] progress 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 线程安全，总字节数未知时为 0。
 */
XMAX_API xmax_result XMAX_CALL
xmax_storage_get_progress(
    xmax_storage storage,
    xmax_transfer_progress* progress
);

/**
 * 上传编码后的本地图片或视频文件。
 *
 * @param[in] storage 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in] utf8_path UTF-8 本地文件路径，不得为 NULL。
 * @param[in] options 可选上传配置；NULL 使用文件名和 MIME 推断，不启用图片安全检查。
 * @param[out] out_file 接收上传结果句柄，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。图片最大 20 MiB，其他媒体最大 256 MiB；结果由
 * xmax_uploaded_file_release 释放。
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_upload_file(
    xmax_storage storage,
    const char* utf8_path,
    const xmax_upload_options* options,
    xmax_uploaded_file* out_file
);

/**
 * 上传编码文件字节，不接收原始视频像素。
 *
 * @param[in] storage 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in] data 本次调用期间有效的编码文件字节，不得为 NULL。
 * @param[in] length 可访问字节数，须大于 0，并符合对应媒体大小限制。
 * @param[in] options 带 struct_size 的上传配置，必须提供文件名。
 * @param[out] out_file 接收上传结果句柄，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。输入数据在同步调用返回前必须有效；结果由 xmax_uploaded_file_release
 * 释放。
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_upload_data(
    xmax_storage storage,
    const uint8_t* data,
    size_t length,
    const xmax_upload_options* options,
    xmax_uploaded_file* out_file
);

/**
 * 将 HTTPS 内容下载到尚不存在的本地文件。
 *
 * @param[in] storage 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in] url 完整 HTTPS 地址，不得为 NULL；不接受重定向。
 * @param[in] utf8_destination UTF-8 目标文件路径，不得为 NULL，已存在时返回错误。
 * @param[out] byte_count 接收完成文件字节数的输出指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。下载最大 256 MiB，成功后才发布目标文件。
 */
XMAX_API xmax_result XMAX_CALL xmax_storage_download(
    xmax_storage storage,
    const char* url,
    const char* utf8_destination,
    uint64_t* byte_count
);

/**
 * 借用上传结果中的文件元数据。
 *
 * @param[in] file 上传结果句柄，不得为 NULL。
 * @param[in,out] info 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 字符串在上传结果释放前有效；未返回 ETag 时对应指针为 NULL。
 */
XMAX_API xmax_result XMAX_CALL
xmax_uploaded_file_get_info(
    xmax_uploaded_file file,
    xmax_uploaded_file_info* info
);

/**
 * 释放上传结果及其元数据字符串。
 *
 * @param[in] file 待释放结果句柄，可为 NULL。
 */
XMAX_API void XMAX_CALL xmax_uploaded_file_release(
    xmax_uploaded_file file
);

/**
 * 在模型对应 API 根地址下执行鉴权 JSON 请求。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[in] method HTTP 方法，支持 GET、POST、PUT 或 DELETE。
 * @param[in] path 相对 API 路径，不得使用其他主机的完整地址。
 * @param[in] body 可选 UTF-8 JSON 请求体，NULL 表示不发送请求体。
 * @param[out] out_json 接收响应 data 负载的 JSON 文本句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。响应文本由 xmax_string_release 释放；不返回完整的响应封装。
 */
XMAX_API xmax_result XMAX_CALL xmax_client_request_json(
    xmax_client client,
    const char* method,
    const char* path,
    const char* body,
    xmax_string* out_json
);

/**
 * 借用 SDK 字符串的 UTF-8 内容。
 *
 * @param[in] string SDK 字符串句柄，可为 NULL。
 * @return 以 NUL 结尾的借用文本；空句柄返回 NULL，不得修改或释放。
 * @note 文本在对应字符串句柄释放前有效。
 */
XMAX_API const char* XMAX_CALL xmax_string_data(
    xmax_string string
);

/**
 * 释放 SDK 字符串及其文本存储。
 *
 * @param[in] string 待释放字符串句柄，可为 NULL。
 */
XMAX_API void XMAX_CALL xmax_string_release(
    xmax_string string
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
#ifdef __cplusplus
}
#endif
#endif
