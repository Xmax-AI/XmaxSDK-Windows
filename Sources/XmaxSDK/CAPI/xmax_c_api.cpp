//
//  xmax_c_api.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/XmaxClient.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
using namespace xmax::detail;

/**
 * 保存 C 客户端句柄对应的配置与服务入口，由 xmax_client_destroy 释放。
 */
struct xmax_client_impl {

    /**
     * 由当前 C 客户端句柄独占拥有的内部客户端。
     */
    XmaxClient client;

    /**
     * 复制客户端配置并交由当前不透明句柄管理。
     *
     * @param[in] config 复制后保留的 API 凭证、模型标识和 HTTP 超时配置。
     */
    explicit xmax_client_impl(
        Config config
    )
        : client(std::move(config)) {
    }
};

/**
 * 保存实时会话句柄及其独占控制器，由 xmax_realtime_destroy 释放。
 */
struct xmax_realtime_impl {

    /**
     * 由当前 C 实时句柄独占拥有的实时控制器。
     */
    std::unique_ptr<XmaxRealtimeManager> session;

    /**
     * 接管实时控制器并交由当前不透明句柄管理。
     *
     * @param[in] value 由当前句柄接管独占所有权的实时控制器。
     */
    explicit xmax_realtime_impl(
        std::unique_ptr<XmaxRealtimeManager> value
    )
        : session(std::move(value)) {
    }
};

/**
 * 拥有一次摄像头枚举快照；设备名称和标识在快照释放前有效。
 */
struct xmax_camera_list_impl {

    /**
     * 当前枚举快照独占拥有的摄像头信息集合。
     */
    std::vector<CameraDevice> devices;
};

/**
 * 拥有存储传输控制器，由 xmax_storage_destroy 释放。
 */
struct xmax_storage_impl {

    /**
     * 由当前 C 存储句柄独占拥有的传输控制器。
     */
    std::unique_ptr<XmaxStorageManager> storage;
};

/**
 * 保存上传成功后的对象元数据，由 xmax_uploaded_file_release 释放。
 */
struct xmax_uploaded_file_impl {

    /**
     * 由上传结果句柄拥有的存储对象元数据。
     */
    StoredFile file;
};

/**
 * 持有独立的帧所有权，使像素在帧句柄释放前保持有效。
 */
struct xmax_frame_impl {

    /**
     * 共享的视频帧引用，确保句柄释放前像素数据一直有效。
     */
    std::shared_ptr<Frame> frame;
};

/**
 * 拥有以 NUL 结尾的 UTF-8 返回文本，由 xmax_string_release 释放。
 */
struct xmax_string_impl {

    /**
     * 由字符串句柄拥有的 UTF-8 文本。
     */
    std::string value;
};

namespace {

/**
 * 当前线程最近一次 SDK 操作的诊断缓冲区，以 NUL 结尾。
 */
thread_local char last_error[1024]{};

/**
 * 当前线程最近一次失败的结构化诊断。
 */
thread_local xmax_error_info last_error_detail{};

/**
 * 在当前线程保存经过脱敏且不会截断 UTF-8 字符的错误文本。
 *
 * @param[in] message 以 NUL 结尾的原始诊断文本，由本方法执行脱敏并截取到线程缓冲区。
 */
void set_error(
    const char* message
) noexcept {
    try {
        const auto safe = XmaxLogger::redact(message);
        auto length = std::min(safe.size(), sizeof(last_error) - 1);

        while (length && length < safe.size() && (static_cast<unsigned char>(safe[length]) & 0xc0) == 0x80)
            --length;

        std::memcpy(last_error, safe.data(), length);
        last_error[length] = 0;
    } catch (...) {
        std::strcpy(last_error, "SDK operation failed; diagnostic formatting unavailable");
    }
}

/**
 * 记录失败的 C 接口操作，避免日志轮询自身产生递归诊断。
 *
 * @param[in] operation 失败的 C 接口名称，用于错误日志与轮询递归保护。
 * @param[in] code 本次 C 接口返回的负数 SDK 错误码。
 */
void log_failure(
    const char* operation,
    xmax_result code
) noexcept {
    if (std::strncmp(operation, "xmax_logger_", 12) == 0)
        return;
    XmaxLogger::write(
        LogCategory::API, code == XMAX_CANCELLED ? XMAX_LOG_WARNING : XMAX_LOG_ERROR, [&](auto language) {
            return std::string(operation) + "\n├─ " +
                   XmaxLogger::localized(language, "错误码：", "Error Code: ") + std::to_string(code) +
                   "\n└─ " + XmaxLogger::localized(language, "原因：", "Reason: ") + last_error;
        }
    );
}

/**
 * 在 C ABI 边界捕获异常，转换为错误码与线程局部诊断。
 *
 * @tparam F 待执行操作的可调用对象类型。
 * @param[in] operation 当前 C 接口名称，用于失败诊断与日志分类。
 * @param[in] f 需在 C ABI 异常边界内执行的操作。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
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
    std::memcpy(last_error_detail.message, last_error, sizeof(last_error));
    log_failure(operation, code);

    return code;
}

/**
 * 在访问 ABI 结构体前校验其指针和 struct_size。
 *
 * @tparam T 参与该操作的输入类型。
 * @param[in] value 需校验的 ABI 结构体指针，struct_size 不得小于当前结构体大小。
 */
template <class T>
void sized(
    const T* value
) {
    require(
        value && value->struct_size >= sizeof(T), XMAX_INVALID_ARGUMENT,
        "Missing or incompatible structure size"
    );
}

/**
 * 校验必需的句柄或输出指针，拒绝空指针。
 *
 * @param[in] p 必需的句柄或输出指针，不得为空。
 */
void handle(
    const void* p
) {
    require(p != nullptr, XMAX_INVALID_ARGUMENT, "Handle or output pointer is null");
}

/**
 * 删除复制后 API 凭证首尾的空白字符。
 *
 * @param[in] s 需要移除首尾空白的已复制 API 凭证文本。
 */
std::string trim(
    std::string s
) {
    auto ws = [](unsigned char c) { return std::isspace(c); };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), ws));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), ws).base(), s.end());

    return s;
}
} // namespace

extern "C" {

/**
 * 查询模型支持的规格、像素限制和默认配置。
 *
 * @param[in] model 模型标识；NULL 使用 x2.1。
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_model_get_info(
    const char* model,
    xmax_model_info* info
) {
    return protect(__func__, [&] {
        sized(info);
        *info = RealtimeModelDefinition::info(RealtimeModelDefinition::parse(model ? model : "x2.1"));
    });
}

/**
 * 复制全局配置并创建客户端，不启动采集或云端会话。
 *
 * @param[in] options 全局配置，需指定 ABI 版本和服务环境。
 * @param[out] out_client 接收客户端；失败时置为 NULL，由 xmax_client_destroy 释放。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_client_create_with_options(
    const xmax_client_options* options,
    xmax_client* out_client
) {
    if (out_client)
        *out_client = nullptr;
    return protect(__func__, [&] {
        sized(options);
        handle(out_client);
        require(options->abi_version == XMAX_ABI_VERSION, XMAX_INVALID_ARGUMENT, "Unsupported C ABI version");
        Config config;
        config.key = trim(options->api_key ? options->api_key : "");
        config.model = "x2.1";
        config.environment = options->environment;
        config.timeout_ms = options->http_timeout_ms ? options->http_timeout_ms : 15000;
        auto client = std::make_unique<xmax_client_impl>(std::move(config));
        XmaxLogger::configure(
            options->logger_options,
            options->environment == XMAX_ENVIRONMENT_CHINA ? XMAX_LOG_CHINESE : XMAX_LOG_ENGLISH
        );
        *out_client = client.release();
    });
}

/**
 * 使用独立模型配置创建实时控制器。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[in] config 实时配置，创建时复制。
 * @param[out] out_realtime 接收实时句柄；失败时置为 NULL，由 xmax_realtime_destroy 释放。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_create_with_config(
    xmax_client client,
    const xmax_realtime_config* config,
    xmax_realtime* out_realtime
) {
    if (out_realtime)
        *out_realtime = nullptr;
    return protect(__func__, [&] {
        handle(client);
        sized(config);
        handle(out_realtime);
        const std::string model = config->model ? config->model : "x2.1";
        RealtimeModelDefinition::parse(model);
        *out_realtime = new xmax_realtime_impl(client->client.create_realtime(model));
    });
}

/**
 * 读取当前线程最近一次操作的结构化错误；本查询不会清除错误。
 *
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
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
 * 读取当前状态、状态原因和关联错误。
 *
 * @param[in] realtime 实时句柄。
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_get_state_info(
    xmax_realtime realtime,
    xmax_event_info* info
) {
    return protect(__func__, [&] {
        handle(realtime);
        sized(info);
        *info = realtime->session->state_info();
    });
}

/**
 * 非阻塞读取包含状态原因和错误详情的最早事件。
 *
 * @param[in] realtime 实时句柄。
 * @param[in,out] info 输出结构体，调用前设置 struct_size。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 队列为空返回 XMAX_NO_EVENT；与 xmax_realtime_poll_event 共用队列，请选择一种消费方式。
 */
xmax_result XMAX_CALL xmax_realtime_poll_event_info(
    xmax_realtime realtime,
    xmax_event_info* info
) {
    bool found = false;
    const auto result = protect(__func__, [&] {
        handle(realtime);
        sized(info);
        found = realtime->session->poll(*info);
    });
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * 按业务原因断开连接，保留本地媒体预览。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] reason 正常停止或方向变化；失败原因由 SDK 报告。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_disconnect_with_reason(
    xmax_realtime realtime,
    xmax_realtime_reason reason
) {
    return protect(__func__, [&] {
        handle(realtime);
        realtime->session->disconnect_reason(reason);
    });
}

/**
 * 更新已就绪且未连接的摄像头编码规格，保留预览绑定和设备选择。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] format 模型支持的新规格，保留已设置的码率和编码偏好。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 操作会阻塞，应在工作线程执行；准备失败时尝试恢复此前规格。
 */
xmax_result XMAX_CALL xmax_realtime_update_camera_format(
    xmax_realtime realtime,
    const xmax_video_format* format
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(format);
        realtime->session->update_camera_format(*format);
    });
}

/**
 * 切换已就绪摄像头的分辨率，保留帧率和编码配置。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] preset 目标分辨率预设，不能为 NONE。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_update_camera_preset(
    xmax_realtime realtime,
    xmax_camera_preset preset
) {
    return protect(__func__, [&] {
        handle(realtime);
        const auto current = realtime->session->camera_format();
        require(current.width != 0, XMAX_INVALID_STATE, "No camera stream is active");
        realtime->session->update_camera_format(RealtimeModelDefinition::preset_format(preset, current.fps));
    });
}

/**
 * 查询摄像头当前编码规格与预设，不随网络调档改变。
 *
 * @param[in] realtime 实时句柄。
 * @param[out] format 接收规格；没有摄像头流时清零。
 * @param[out] preset 接收预设；没有摄像头流时为 NONE。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_get_camera_format(
    xmax_realtime realtime,
    xmax_video_format* format,
    xmax_camera_preset* preset
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(format);
        handle(preset);
        *format = realtime->session->camera_format();
        *preset = RealtimeModelDefinition::preset(*format);
    });
}

/**
 * 在生成中设置上行档位，后续网络自适应仍可继续调档。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] level 目标档位，范围为 [1, 5]；1 画质最高。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 操作会阻塞；失败时保留此前档位。
 */
xmax_result XMAX_CALL xmax_realtime_set_uplink_quality_level(
    xmax_realtime realtime,
    int32_t level
) {
    return protect(__func__, [&] {
        handle(realtime);
        realtime->session->quality_level(0, level);
    });
}

/**
 * 在生成中设置下行档位，后续网络自适应仍可继续调档。
 *
 * @param[in] realtime 实时句柄。
 * @param[in] level 目标档位，范围为 [1, 5]；1 画质最高。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 操作会阻塞；失败时保留此前档位。
 */
xmax_result XMAX_CALL xmax_realtime_set_downlink_quality_level(
    xmax_realtime realtime,
    int32_t level
) {
    return protect(__func__, [&] {
        handle(realtime);
        realtime->session->quality_level(1, level);
    });
}

/**
 * 提交轨迹采样，由 SDK 映射视频坐标并异步发送当前任务的 tracks 信令。
 *
 * @param[in] realtime 正在生成的实时句柄。
 * @param[in] frame 当前视口的轨迹采样，调用时同步复制。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 * @note 采样采用最新值合并，适应模式的黑边采样会被忽略；成功表示已接收，不表示服务端已处理。
 */
xmax_result XMAX_CALL xmax_realtime_submit_interaction(
    xmax_realtime realtime,
    const xmax_interaction_frame* frame
) {
    return protect(__func__, [&] {
        handle(realtime);
        sized(frame);
        realtime->session->submit_interaction(*frame);
    });
}

/**
 * 复制当前任务尚未过期的归一化轨迹，供界面绘制自定义效果。
 *
 * @param[in] realtime 实时句柄。
 * @param[out] points 输出点数组；capacity 为 0 时允许为 NULL。
 * @param[in] capacity 数组容量，最多需要 256 个元素。
 * @param[out] count 接收所需点数；容量不足返回 XMAX_BUFFER_TOO_SMALL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_get_trajectory(
    xmax_realtime realtime,
    xmax_point* points,
    size_t capacity,
    size_t* count
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(count);
        const auto values = realtime->session->trajectory();
        *count = values.size();
        require(capacity >= values.size(), XMAX_BUFFER_TOO_SMALL, "Trajectory buffer is too small");
        if (!values.empty()) {
            handle(points);
            std::copy(values.begin(), values.end(), points);
        }
    });
}

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
xmax_result XMAX_CALL xmax_realtime_render_window(
    xmax_realtime realtime,
    xmax_frame frame,
    void* window,
    int32_t content_mode
) {
    return protect(__func__, [&] {
        handle(realtime);
        handle(frame);
        realtime->session->render_window(*frame->frame, window, content_mode);
    });
}

/**
 * 获取 SDK 的语义版本号。
 *
 * @return 静态 UTF-8 版本文本，不得修改或释放。
 */
const char* XMAX_CALL xmax_version(
    void
) {
    return XMAX_VERSION;
}

/**
 * 获取当前线程最近一次 SDK 操作的诊断文本。
 *
 * @return 借用的 NUL 结尾 UTF-8 文本，不得释放；需保留时应立即复制。
 * @note 诊断在当前线程下一次 SDK 操作前有效；不同线程的错误文本相互独立。
 */
const char* XMAX_CALL xmax_last_error(
    void
) {
    return last_error;
}

/**
 * 配置当前进程所有客户端共享的日志选项与语言。
 *
 * @param[in] options 日志位掩码，可组合 XMAX_LOG_BUSINESS 和 XMAX_LOG_PERFORMANCE；XMAX_LOG_NONE 表示关闭。
 * @param[in] language 后续记录使用的中文或英文输出语言。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 线程安全。日志默认关闭，每次配置变更会清空待消费记录。
 */
xmax_result XMAX_CALL xmax_logger_configure(
    uint32_t options,
    xmax_log_language language
) {
    return protect(__func__, [&] { XmaxLogger::configure(options, language); });
}

/**
 * 读取当前进程共享的日志配置。
 *
 * @param[out] options 接收已启用日志位掩码的输出指针，不得为 NULL。
 * @param[out] language 接收输出语言的指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 线程安全，创建客户端不会重置该配置。
 */
xmax_result XMAX_CALL xmax_logger_get_config(
    uint32_t* options,
    xmax_log_language* language
) {
    return protect(__func__, [&] {
        handle(options);
        handle(language);

        const auto config = XmaxLogger::configuration();
        *options = config.options;
        *language = config.language;
    });
}

/**
 * 非阻塞取出进程级日志队列中最早的记录。
 *
 * @param[in,out] record 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 * @note 进程级日志队列应由单一消费者读取；最多保留最新的 256 条记录。
 */
xmax_result XMAX_CALL xmax_logger_poll(
    xmax_log_record* record
) {
    bool found = false;
    const auto result = protect(__func__, [&] {
        sized(record);
        found = XmaxLogger::poll(*record);
    });
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * 复制配置并创建客户端，不启动摄像头或云端会话。
 *
 * @param[in] c 客户端配置，需设置 struct_size 和 XMAX_ABI_VERSION；空 API 凭证允许进行本地准备。
 * @param[out] out 接收客户端句柄的输出指针，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回句柄由 xmax_client_destroy 释放；输入字符串在创建时复制。
 */
xmax_result XMAX_CALL xmax_client_create(
    const xmax_client_config* c,
    xmax_client* out
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        sized(c);
        handle(out);
        require(c->abi_version == XMAX_ABI_VERSION, XMAX_INVALID_ARGUMENT, "Unsupported C ABI version");

        Config config;
        config.key = trim(c->api_key ? c->api_key : "");
        config.model = c->model ? c->model : "x2.1";
        config.timeout_ms = c->http_timeout_ms ? c->http_timeout_ms : 15000;

        auto client = std::make_unique<xmax_client_impl>(std::move(config));
        *out = client.release();
    });
}

/**
 * 释放客户端句柄及其独占资源。
 *
 * @param[in] client 待释放句柄，可为 NULL。
 * @note 释放前应结束使用该句柄的并发调用；已创建的子控制器独立保留共享配置。
 */
void XMAX_CALL xmax_client_destroy(
    xmax_client client
) {
    delete client;
}

/**
 * 查询当前 SDK 构建是否包含 RTC 后端。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out 接收可用标记的输出指针，不得为 NULL；1 表示可用，0 表示不可用。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
xmax_result XMAX_CALL xmax_client_has_rtc(
    xmax_client client,
    int32_t* out
) {
    return protect(__func__, [&] {
        handle(client);
        handle(out);
        *out = client->client.has_rtc() ? 1 : 0;
    });
}

/**
 * 查询当前 SDK 构建的能力位与 ABI 版本。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
xmax_result XMAX_CALL xmax_client_get_capabilities(
    xmax_client client,
    xmax_capabilities* out
) {
    return protect(__func__, [&] {
        handle(client);
        sized(out);
        *out = {sizeof(*out), XMAX_ABI_VERSION, client->client.capabilities()};
    });
}

/**
 * 枚举摄像头设备，不启动采集或云端会话。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out 接收枚举快照句柄的输出指针，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 快照由 xmax_camera_list_release 释放。
 */
xmax_result XMAX_CALL xmax_client_list_cameras(
    xmax_client client,
    xmax_camera_list* out
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        handle(client);
        handle(out);
        *out = new xmax_camera_list_impl{client->client.cameras()};
    });
}

/**
 * 获取摄像头枚举快照中的设备数量。
 *
 * @param[in] list 枚举快照句柄，可为 NULL。
 * @return 设备数量；句柄为 NULL 时返回 0。
 */
size_t XMAX_CALL xmax_camera_list_count(
    xmax_camera_list list
) {
    return list ? list->devices.size() : 0;
}

/**
 * 读取枚举快照中指定摄像头的信息。
 *
 * @param[in] list 有效枚举快照句柄，不得为 NULL。
 * @param[in] index 从 0 开始的设备索引，必须小于设备数量。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回的设备字符串由枚举快照拥有，在快照释放前有效。
 */
xmax_result XMAX_CALL xmax_camera_list_get(
    xmax_camera_list list,
    size_t index,
    xmax_camera_info* out
) {
    return protect(__func__, [&] {
        handle(list);
        sized(out);
        require(index < list->devices.size(), XMAX_INVALID_ARGUMENT, "Camera index is out of range");

        const auto& device = list->devices[index];
        *out = {sizeof(*out), device.id.c_str(), device.name.c_str(), device.is_default ? 1 : 0};
    });
}

/**
 * 释放摄像头枚举快照及其设备字符串。
 *
 * @param[in] list 待释放的快照句柄，可为 NULL。
 */
void XMAX_CALL xmax_camera_list_release(
    xmax_camera_list list
) {
    delete list;
}

/**
 * 创建独立的实时控制器，不建立云端连接。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out 接收实时控制器句柄的输出指针，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回句柄由 xmax_realtime_destroy 释放，并独立保留客户端服务配置。
 */
xmax_result XMAX_CALL xmax_realtime_create(
    xmax_client client,
    xmax_realtime* out
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        handle(client);
        handle(out);
        *out = new xmax_realtime_impl(client->client.create_realtime());
    });
}

/**
 * 关闭并释放实时控制器的会话、RTC 和媒体资源。
 *
 * @param[in] realtime 待释放的实时控制器句柄，可为 NULL。
 * @note 释放前应结束使用该句柄的并发调用；该操作可能等待资源清理。
 */
void XMAX_CALL xmax_realtime_destroy(
    xmax_realtime realtime
) {
    delete realtime;
}

/**
 * 绑定接入方持续提供视频帧的外部输入。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] f 输入尺寸和帧率，支持范围见 xmax_video_format。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。绑定外部输入会停止此前的本地媒体；需在连接前调用。
 */
xmax_result XMAX_CALL xmax_realtime_prepare_external(
    xmax_realtime r,
    const xmax_video_format* f
) {
    return protect(__func__, [&] {
        handle(r);
        handle(f);
        r->session->prepare(*f);
    });
}

/**
 * 启动 SDK 摄像头采集，并等待第一张真实视频帧。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] config 带 struct_size 的摄像头配置；空设备标识使用默认摄像头，视频规格全部为 0 时使用
 * 当前模型的摄像头默认规格。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。仅准备本地视频，不创建服务端会话；麦克风在后续连接时按配置启动。
 */
xmax_result XMAX_CALL xmax_realtime_prepare_camera(
    xmax_realtime r,
    const xmax_camera_config* config
) {
    return protect(__func__, [&] {
        handle(r);
        sized(config);

        auto format = config->format;

        if (!format.width && !format.height && !format.fps)
            format = r->session->model_info().default_camera_format;

        r->session->prepare_camera(
            config->device_id ? config->device_id : "", format, config->use_microphone != 0
        );
    });
}

/**
 * 切换活动摄像头，保留连接并恢复已有生成条件。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] device UTF-8 设备标识；空文本使用默认摄像头，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。等待新摄像头就绪；失败时清理连接与本地媒体并返回错误。
 */
xmax_result XMAX_CALL xmax_realtime_switch_camera(
    xmax_realtime r,
    const char* device
) {
    return protect(__func__, [&] {
        handle(r);
        handle(device);
        r->session->switch_camera(device);
    });
}

/**
 * 释放断开连接后的本地采集和输入绑定。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。存在活动连接时应先断开连接。
 */
xmax_result XMAX_CALL xmax_realtime_stop_media(
    xmax_realtime r
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->stop_media();
    });
}

/**
 * 创建服务端会话并加入 RTC，暂不启动生成。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] microphone 麦克风开关，非零表示加入房间时启用。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。调用前必须准备本地媒体；摄像头配置中的麦克风选项同样生效。
 */
xmax_result XMAX_CALL xmax_realtime_connect(
    xmax_realtime r,
    int32_t microphone
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->connect(microphone != 0);
    });
}

/**
 * 使用指定或缓存条件启动生成，并等待结果首帧。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] context 生成条件，字符串在本次同步调用返回前必须有效。可为 NULL 以复用条件，但首次生成必须提供。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。存在就绪媒体但尚未连接时会先连接；外部输入由其他线程持续供帧。
 */
xmax_result XMAX_CALL xmax_realtime_generate(
    xmax_realtime r,
    const xmax_generation_context* context
) {
    return protect(__func__, [&] {
        handle(r);
        if (context) {
            sized(context);
            handle(context->prompt);
        }
        r->session->generate(context);
    });
}

/**
 * 读取当前状态和会话标识快照，不消费待处理事件。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此查询可能等待串行生命周期队列。
 */
xmax_result XMAX_CALL xmax_realtime_get_state(
    xmax_realtime r,
    xmax_event* out
) {
    return protect(__func__, [&] {
        handle(r);
        sized(out);
        *out = r->session->state_snapshot();
    });
}

/**
 * 读取外部输入规格或摄像头的 RTC 编码目标，实际采集尺寸以本地帧视图为准。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[out] out 接收输入规格的输出指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此查询可能等待串行生命周期队列。
 */
xmax_result XMAX_CALL xmax_realtime_get_format(
    xmax_realtime r,
    xmax_video_format* out
) {
    return protect(__func__, [&] {
        handle(r);
        handle(out);
        *out = r->session->format();
    });
}

/**
 * 读取当前或下次连接使用的远端播放音量。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[out] out 接收 [0, 1] 范围音量的输出指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 初始音量为 0；此查询可能等待串行生命周期队列。
 */
xmax_result XMAX_CALL xmax_realtime_get_remote_volume(
    xmax_realtime r,
    float* out
) {
    return protect(__func__, [&] {
        handle(r);
        handle(out);
        *out = r->session->volume();
    });
}

/**
 * 配置上行码率限制与编码降级偏好。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] config 带 struct_size 的编码配置；最小码率 -1 使用策略默认值，最大码率 0 使用策略估计值。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。已连接时更新当前原生编码器。
 */
xmax_result XMAX_CALL xmax_realtime_set_encoding(
    xmax_realtime r,
    const xmax_encoding_config* config
) {
    return protect(__func__, [&] {
        handle(r);
        sized(config);
        r->session->encoding(*config);
    });
}

/**
 * 开启或关闭独立的双向画质自适应。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] enabled 非零启用，0 关闭；变更时重置网络质量观察窗口。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
xmax_result XMAX_CALL xmax_realtime_set_adaptive_quality(
    xmax_realtime r,
    int32_t enabled
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->adaptive(enabled != 0);
    });
}

/**
 * 读取当前双向画质档位和生效规格。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此查询可能等待串行生命周期队列。
 */
xmax_result XMAX_CALL xmax_realtime_get_quality(
    xmax_realtime r,
    xmax_quality_status* out
) {
    return protect(__func__, [&] {
        handle(r);
        sized(out);
        *out = r->session->quality();
    });
}

/**
 * 请求开启或关闭帧插值。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] enabled 非零表示请求启用帧插值。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 缺少原生插值后端时，启用请求返回 XMAX_UNSUPPORTED；关闭请求仍可成功。
 */
xmax_result XMAX_CALL xmax_realtime_set_interpolation(
    xmax_realtime r,
    int32_t enabled
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->interpolation(enabled != 0);
    });
}

/**
 * 使用初始条件连接并启动生成，等待结果首帧。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] c 生成条件，字符串在本次同步调用返回前必须有效。不得为 NULL。
 * @param[in] microphone 麦克风开关，非零表示连接时启用。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。调用前必须准备媒体；外部视频输入必须由其他线程持续提交。
 */
xmax_result XMAX_CALL xmax_realtime_start(
    xmax_realtime r,
    const xmax_generation_context* c,
    int32_t microphone
) {
    return protect(__func__, [&] {
        handle(r);
        sized(c);
        handle(c->prompt);
        r->session->start(
            c->prompt, c->reference_path ? c->reference_path : "", c->face_restoration != 0, microphone != 0
        );
    });
}

/**
 * 替换正在运行的生成任务条件。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] c 生成条件，字符串在本次同步调用返回前必须有效。不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。要求当前已处于生成状态。
 */
xmax_result XMAX_CALL xmax_realtime_update(
    xmax_realtime r,
    const xmax_generation_context* c
) {
    return protect(__func__, [&] {
        handle(r);
        sized(c);
        handle(c->prompt);
        r->session->update(c->prompt, c->reference_path ? c->reference_path : "", c->face_restoration != 0);
    });
}

/**
 * 断开服务端与 RTC 连接，保留本地预览和缓存条件。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。同时请求取消在途生命周期操作。
 */
xmax_result XMAX_CALL xmax_realtime_disconnect(
    xmax_realtime r
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->disconnect(false);
    });
}

/**
 * 断开连接并释放本地媒体和缓存生成条件。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。控制器句柄仍有效，可再次准备媒体并启动新会话。
 */
xmax_result XMAX_CALL xmax_realtime_close(
    xmax_realtime r
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->disconnect(true);
    });
}

/**
 * 查询当前是否需要接入方提供外部视频帧。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[out] out 接收请求标记的输出指针，不得为 NULL；摄像头输入模式始终为 0。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 该标记不承担外部源帧率控制；查询不等待生命周期操作。
 */
xmax_result XMAX_CALL xmax_realtime_is_requesting_frames(
    xmax_realtime r,
    int32_t* out
) {
    return protect(__func__, [&] {
        handle(r);
        handle(out);
        *out = r->session->requesting() ? 1 : 0;
    });
}

/**
 * 同步复制并提交一帧外部输入视频。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] f 带 struct_size 的有效视频视图，尺寸须匹配绑定规格，时间戳须严格递增。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回后可复用原像素内存。绑定 SDK 摄像头时不能提交外部帧；未连接时也可用于本地预览。
 */
xmax_result XMAX_CALL xmax_realtime_push_video_frame(
    xmax_realtime r,
    const xmax_video_frame* f
) {
    return protect(__func__, [&] {
        handle(r);
        sized(f);
        r->session->push(*f);
    });
}

/**
 * 设置当前或下次连接使用的远端播放音量。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in] v 范围为 [0, 1] 的有限数值，0 表示静音。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 */
xmax_result XMAX_CALL xmax_realtime_set_remote_volume(
    xmax_realtime r,
    float v
) {
    return protect(__func__, [&] {
        handle(r);
        r->session->volume(v);
    });
}

/**
 * 非阻塞消费最早的待处理应用事件。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 */
xmax_result XMAX_CALL xmax_realtime_poll_event(
    xmax_realtime r,
    xmax_event* out
) {
    bool found = false;
    auto result = protect(__func__, [&] {
        handle(r);
        sized(out);
        found = r->session->poll(*out);
    });
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * 非阻塞取出最新远端结果帧。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[out] out 接收视频帧句柄的输出指针，不得为 NULL；像素在句柄释放前有效，无帧或失败时输出句柄置为
 * NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 * @note 返回帧由 xmax_frame_release 释放；新帧替换尚未消费的旧帧。
 */
xmax_result XMAX_CALL xmax_realtime_acquire_frame(
    xmax_realtime r,
    xmax_frame* out
) {
    if (out)
        *out = nullptr;

    bool found = false;
    auto result = protect(__func__, [&] {
        handle(r);
        handle(out);

        auto frame = r->session->acquire_frame();

        if (frame) {
            *out = new xmax_frame_impl{std::move(frame)};
            found = true;
        }
    });

    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * 非阻塞取出最新本地预览帧。
 *
 * @param[in] r 实时控制器句柄，不得为 NULL。
 * @param[out] out 接收视频帧句柄的输出指针，不得为 NULL；像素在句柄释放前有效，无帧或失败时输出句柄置为
 * NULL。
 * @return 成功取得数据返回 XMAX_OK；当前无待处理数据返回 XMAX_NO_EVENT；失败返回负数错误码。
 * @note 返回帧由 xmax_frame_release 释放；取帧不会等待媒体采集。
 */
xmax_result XMAX_CALL xmax_realtime_acquire_local_frame(
    xmax_realtime r,
    xmax_frame* out
) {
    if (out)
        *out = nullptr;

    bool found = false;
    const auto result = protect(__func__, [&] {
        handle(r);
        handle(out);

        auto frame = r->session->acquire_local_frame();

        if (frame) {
            *out = new xmax_frame_impl{std::move(frame)};
            found = true;
        }
    });

    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}

/**
 * 在目标 HWND 所属线程呈现一帧视频。
 *
 * @param[in] frame 有效帧句柄，不得为 NULL。
 * @param[in] hwnd 目标 Windows HWND 转换为 void* 后的值。
 * @param[in] mode XMAX_CONTENT_FIT 等比例适应，或 XMAX_CONTENT_FILL 居中填满。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 必须从 HWND 所属线程调用；不支持原生窗口的平台返回 XMAX_UNSUPPORTED。
 */
xmax_result XMAX_CALL xmax_frame_render_window(
    xmax_frame frame,
    void* hwnd,
    int32_t mode
) {
    return protect(__func__, [&] {
        handle(frame);
        render_window(*frame->frame, hwnd, mode);
    });
}

/**
 * 创建独立的存储传输控制器。
 *
 * @param[in] client 客户端句柄，不得为 NULL。
 * @param[out] out 接收存储控制器句柄，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 返回句柄由 xmax_storage_destroy 释放，并独立保留客户端配置。
 */
xmax_result XMAX_CALL xmax_storage_create(
    xmax_client client,
    xmax_storage* out
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        handle(client);
        handle(out);
        *out = new xmax_storage_impl{client->client.create_storage()};
    });
}

/**
 * 释放存储传输控制器。
 *
 * @param[in] value 待释放的存储句柄，可为 NULL。
 * @note 释放前应取消并等待在途传输退出，结束使用句柄的并发调用。
 */
void XMAX_CALL xmax_storage_destroy(
    xmax_storage value
) {
    delete value;
}

/**
 * 请求取消当前存储传输。
 *
 * @param[in] value 存储控制器句柄，可为 NULL。
 * @note 允许从其他线程调用，不等待传输退出。
 */
void XMAX_CALL xmax_storage_cancel(
    xmax_storage value
) {
    if (value)
        value->storage->cancel();
}

/**
 * 线程安全地读取当前传输状态和字节进度。
 *
 * @param[in] value 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 线程安全，总字节数未知时为 0。
 */
xmax_result XMAX_CALL xmax_storage_get_progress(
    xmax_storage value,
    xmax_transfer_progress* out
) {
    return protect(__func__, [&] {
        handle(value);
        sized(out);
        *out = value->storage->progress();
    });
}

/**
 * 上传编码后的本地图片或视频文件。
 *
 * @param[in] value 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in] path UTF-8 本地文件路径，不得为 NULL。
 * @param[in] options 可选上传配置；NULL 使用文件名和 MIME 推断，不启用图片安全检查。
 * @param[out] out 接收上传结果句柄，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。图片最大 20 MiB，其他媒体最大 256 MiB；结果由
 * xmax_uploaded_file_release 释放。
 */
xmax_result XMAX_CALL xmax_storage_upload_file(
    xmax_storage value,
    const char* path,
    const xmax_upload_options* options,
    xmax_uploaded_file* out
) {
    if (out)
        *out = nullptr;
    return protect(__func__, [&] {
        handle(value);
        handle(path);
        handle(out);

        if (options)
            sized(options);

        const auto name = options && options->file_name ? options->file_name : "";
        const auto type = options && options->content_type ? options->content_type : "";
        auto file =
            value->storage->upload_file(path, name, type, options && options->check_image_safety != 0);
        *out = new xmax_uploaded_file_impl{std::move(file)};
    });
}

/**
 * 上传编码文件字节，不接收原始视频像素。
 *
 * @param[in] value 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in] data 本次调用期间有效的编码文件字节，不得为 NULL。
 * @param[in] length 可访问字节数，须大于 0，并符合对应媒体大小限制。
 * @param[in] options 带 struct_size 的上传配置，必须提供文件名。
 * @param[out] out 接收上传结果句柄，不得为 NULL；失败时置为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。输入数据在同步调用返回前必须有效；结果由 xmax_uploaded_file_release
 * 释放。
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
    return protect(__func__, [&] {
        handle(value);
        handle(data);
        sized(options);
        handle(options->file_name);
        handle(out);

        auto file = value->storage->upload_data(
            data, length, options->file_name, options->content_type ? options->content_type : "",
            options->check_image_safety != 0
        );
        *out = new xmax_uploaded_file_impl{std::move(file)};
    });
}

/**
 * 将 HTTPS 内容下载到尚不存在的本地文件。
 *
 * @param[in] value 存储控制器句柄，不得为 NULL；同一实例只允许一次活动传输。
 * @param[in] url 完整 HTTPS 地址，不得为 NULL；不接受重定向。
 * @param[in] destination UTF-8 目标文件路径，不得为 NULL，已存在时返回错误。
 * @param[out] bytes 接收完成文件字节数的输出指针，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。下载最大 256 MiB，成功后才发布目标文件。
 */
xmax_result XMAX_CALL xmax_storage_download(
    xmax_storage value,
    const char* url,
    const char* destination,
    uint64_t* bytes
) {
    if (bytes)
        *bytes = 0;
    return protect(__func__, [&] {
        handle(value);
        handle(url);
        handle(destination);
        handle(bytes);
        *bytes = value->storage->download(url, destination);
    });
}

/**
 * 借用上传结果中的文件元数据。
 *
 * @param[in] file 上传结果句柄，不得为 NULL。
 * @param[in,out] out 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 字符串在上传结果释放前有效；未返回 ETag 时对应指针为 NULL。
 */
xmax_result XMAX_CALL xmax_uploaded_file_get_info(
    xmax_uploaded_file file,
    xmax_uploaded_file_info* out
) {
    return protect(__func__, [&] {
        handle(file);
        sized(out);
        *out = {
            sizeof(*out), file->file.url.c_str(), file->file.object_key.c_str(),
            file->file.etag.empty() ? nullptr : file->file.etag.c_str()
        };
    });
}

/**
 * 释放上传结果及其元数据字符串。
 *
 * @param[in] file 待释放结果句柄，可为 NULL。
 */
void XMAX_CALL xmax_uploaded_file_release(
    xmax_uploaded_file file
) {
    delete file;
}

/**
 * 借用帧的只读像素布局。
 *
 * @param[in] frame 有效帧句柄，不得为 NULL。
 * @param[in,out] view 输出结构体，调用前设置 struct_size，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 视图不持有额外像素所有权；释放帧句柄后不得继续访问视图。
 */
xmax_result XMAX_CALL xmax_frame_get_view(
    xmax_frame frame,
    xmax_video_frame* view
) {
    return protect(__func__, [&] {
        handle(frame);
        sized(view);
        *view = frame->frame->view();
    });
}

/**
 * 释放帧句柄及其持有的像素生命周期。
 *
 * @param[in] frame 待释放帧句柄，可为 NULL。
 */
void XMAX_CALL xmax_frame_release(
    xmax_frame frame
) {
    delete frame;
}

/**
 * 在模型对应 API 根地址下执行鉴权 JSON 请求。
 *
 * @param[in] c 客户端句柄，不得为 NULL。
 * @param[in] method HTTP 方法，支持 GET、POST、PUT 或 DELETE。
 * @param[in] path 相对 API 路径，不得使用其他主机的完整地址。
 * @param[in] body 可选 UTF-8 JSON 请求体，NULL 表示不发送请求体。
 * @param[out] out 接收响应 data 负载的 JSON 文本句柄，不得为 NULL。
 * @return 成功返回 XMAX_OK；失败返回负数错误码，详情可通过 xmax_last_error 获取。
 * @note 此操作会阻塞，应在工作线程执行。响应文本由 xmax_string_release 释放；不返回完整的响应封装。
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
    return protect(__func__, [&] {
        handle(c);
        handle(method);
        handle(path);
        handle(out);

        json parsed;

        if (body)
            parsed = json::parse(body);

        auto data = c->client.request(method, path, body ? &parsed : nullptr, [] { return false; });
        *out = new xmax_string_impl{data.dump()};
    });
}

/**
 * 借用 SDK 字符串的 UTF-8 内容。
 *
 * @param[in] value SDK 字符串句柄，可为 NULL。
 * @return 以 NUL 结尾的借用文本；空句柄返回 NULL，不得修改或释放。
 * @note 文本在对应字符串句柄释放前有效。
 */
const char* XMAX_CALL xmax_string_data(
    xmax_string value
) {
    return value ? value->value.c_str() : nullptr;
}

/**
 * 释放 SDK 字符串及其文本存储。
 *
 * @param[in] value 待释放字符串句柄，可为 NULL。
 */
void XMAX_CALL xmax_string_release(
    xmax_string value
) {
    delete value;
}
}
