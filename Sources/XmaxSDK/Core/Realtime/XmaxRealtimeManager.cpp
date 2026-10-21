//
//  XmaxRealtimeManager.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Service/Media/MediaService.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {

/**
 * 将事件文本复制到固定容量的缓冲区，始终保留结尾 NUL。
 *
 * @param[out] out 接收事件文本的可写缓冲区。
 * @param[in] size 目标缓冲区总字节容量，需包含结尾 NUL。
 * @param[in] text 需要复制到固定容量事件字段的 UTF-8 文本。
 */
static void text_copy(
    char* out,
    size_t size,
    const std::string& text
) {
    auto count = std::min(size - 1, text.size());

    while (count && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80)
        --count;

    std::memcpy(out, text.data(), count);
    out[count] = 0;
}

/**
 * 使用共享 API 服务及原生工厂构建独立的实时生命周期控制器。
 *
 * @param[in] api 共享 API 服务，控制器在其生命周期内保留该引用。
 * @param[in] factory 创建底层原生适配器的工厂，不得返回空对象。
 * @param[in] heartbeat 活动会话心跳间隔，单位为毫秒。
 * @param[in] camera 创建原生摄像头适配器的工厂。
 * @param[in] history 可选的按模型和方向保存的画质历史，空引用表示不持久化。
 */
XmaxRealtimeManager::XmaxRealtimeManager(
    std::shared_ptr<ApiService> api,
    RtcFactory factory,
    std::chrono::milliseconds heartbeat,
    CameraFactory camera,
    std::shared_ptr<QualityHistory> history
)
    : api_(std::move(api)), connection_(api_), stream_(std::move(factory), std::move(history)),
      media_(std::move(camera)), heartbeat_interval_(heartbeat),
      coordinator_([this] { this->heartbeat(); }, [this](Error error) { fail(std::move(error)); }) {
    require(bool(api_), XMAX_INVALID_ARGUMENT, "API service is required");
    require(heartbeat.count() > 0, XMAX_INVALID_ARGUMENT, "Invalid heartbeat interval");
}

/**
 * 取消在途操作，关闭媒体与 RTC 资源并停止生命周期执行器。
 */
XmaxRealtimeManager::~XmaxRealtimeManager() {
    try {
        disconnect(true);
    } catch (...) {
    }
    coordinator_.shutdown();
}

/**
 * 更新会话状态，并向接入方投递对应状态事件。
 *
 * @param[in] state 新的实时生命周期状态。
 */
void XmaxRealtimeManager::change_state(
    xmax_state state
) {
    {
        std::lock_guard<std::mutex> lock(interaction_mutex_);
        interaction_task_ = state == XMAX_GENERATING ? stream_.task() : "";
        if (state != XMAX_GENERATING)
            render_.trajectory.clear();
    }
    coordinator_.state(state);
    if (!connection_.active().id.empty())
        last_session_id_ = connection_.active().id;
    if (!stream_.task().empty())
        last_task_id_ = stream_.task();

    xmax_event event{};
    event.type = XMAX_EVENT_STATE;
    event.state = state;
    event.code = errors_.error() ? errors_.error()->code : XMAX_OK;
    text_copy(event.session_id, sizeof(event.session_id), last_session_id_);
    text_copy(event.task_id, sizeof(event.task_id), last_task_id_);
    coordinator_.emit(event, errors_.reason(), errors_.error());
    XmaxLogger::write(LogCategory::Realtime, XMAX_LOG_INFO, [&](auto language) {
        static const char* names[]{"idle",       "ready",         "connecting", "connected",
                                   "generating", "disconnecting", "preparing"};
        return XmaxLogger::localized(language, "状态更新 (State Changed)", "State Changed") + "\n├─ " +
               names[state] + "\n├─ session=" + connection_.active().id + "\n└─ task=" + stream_.task();
    });
}

/**
 * 绑定外部视频输入，并进入媒体就绪状态。
 *
 * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
 */
void XmaxRealtimeManager::prepare(
    xmax_video_format format
) {
    coordinator_.sync([this, format] {
        require(
            coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
            "Disconnect before replacing an external source"
        );

        const auto resolved = MediaService::input(format);
        require(
            resolved.width >= 4 && resolved.height >= 4, XMAX_INVALID_ARGUMENT,
            "External video dimensions must be at least 4 pixels"
        );
        errors_.reset();
        change_state(XMAX_PREPARING);
        bind_local();
        media_.prepare_external(resolved);
        camera_microphone_ = false;
        change_state(XMAX_READY);
    });
}

/**
 * 非阻塞检查当前外部输入是否请求供帧。
 *
 * @return 外部输入正在请求供帧时为 true；摄像头模式或未请求供帧时为 false。
 */
bool XmaxRealtimeManager::requesting() const {
    return !media_.camera_active() && input_epoch_.load() == coordinator_.epoch() &&
           media_.external().requesting();
}

/**
 * 将本地预览绑定到新的媒体批次，拒收旧绑定的帧。
 */
void XmaxRealtimeManager::bind_local() {
    const auto token = ++media_epoch_;
    render_.bind_local(token);
    media_.external().preview([this, token](Frame frame) { render_.receive_local(std::move(frame), token); });
}

/**
 * 创建仅处理当前媒体绑定错误的回调，忽略旧绑定的回调。
 *
 * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
 * @return 当前媒体绑定的错误回调；旧绑定触发的错误会被忽略。
 */
std::function<void(Error)> XmaxRealtimeManager::media_failure(
    uint64_t token
) {
    return [this, token](Error error) {
        coordinator_.enqueue([this, token, error] {
            if (media_epoch_ != token || !media_.camera_active())
                return;

            errors_.record(error);
            coordinator_.invalidate();
            cleanup(true);

            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.code = error.code;
            event.state = coordinator_.state();
            text_copy(event.message, sizeof(event.message), XmaxLogger::redact(error.what()));
            coordinator_.emit(event, XMAX_REASON_FAILURE, &error);
        });
    };
}

/**
 * 启动指定摄像头并等待真实首帧，保留实际采集尺寸并进入就绪状态。
 *
 * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
 * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
 * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
 */
void XmaxRealtimeManager::prepare_camera(
    std::string device,
    xmax_video_format format,
    bool microphone
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, device = std::move(device), format, microphone, epoch] {
        require(!coordinator_.cancellation(epoch)(), XMAX_CANCELLED, "Camera startup cancelled");
        require(
            coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
            "Disconnect before replacing local media"
        );

        const auto resolved = MediaService::camera_input(format);
        errors_.reset();
        change_state(XMAX_PREPARING);
        bind_local();

        try {
            media_.prepare_camera(
                device, resolved, media_failure(media_epoch_), coordinator_.cancellation(epoch)
            );
            camera_microphone_ = microphone;
            stream_.volume(0);
            change_state(XMAX_READY);
        } catch (...) {
            try {
                throw;
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(XMAX_INTERNAL_ERROR, "Camera preparation failed"));
            }
            media_.close();
            render_.unbind_local(++media_epoch_);
            change_state(XMAX_IDLE);
            throw;
        }
    });
}

/**
 * 在断开连接后释放本地采集和输入绑定。
 */
void XmaxRealtimeManager::stop_media() {
    coordinator_.sync([this] {
        require(
            coordinator_.state() == XMAX_IDLE || coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
            "Disconnect before stopping local media"
        );
        media_.close();
        render_.unbind_local(++media_epoch_);
        camera_microphone_ = false;
        change_state(XMAX_IDLE);
    });
}

/**
 * 同步复制并提交外部输入帧，允许从媒体线程调用。
 *
 * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
 */
void XmaxRealtimeManager::push(
    const xmax_video_frame& frame
) {
    require(!media_.camera_active(), XMAX_INVALID_STATE, "External frames cannot replace SDK camera capture");
    media_.external().push(frame);
}

/**
 * 创建服务端会话并加入 RTC；取消或失败时回滚已分配资源。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] epoch 调用开始时捕获的取消批次，用于识别失效操作。
 * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
 */
void XmaxRealtimeManager::connect_impl(
    const Cancel& cancel,
    uint64_t epoch,
    bool microphone
) {
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    require(coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE, "Prepare local media before connecting");
    require(!api_->config().key.empty(), XMAX_INVALID_API_KEY, "An Xmax API Key is required");
    errors_.reset();
    timing_.begin();
    change_state(XMAX_CONNECTING);

    connection_.create(stream_, cancel);
    timing_.finish_session();
    timing_.begin_join();

    RtcCallbacks callbacks;
    callbacks.published = [this, epoch, publication = timing_.publication()] {
        if (coordinator_.epoch() != epoch)
            return;

        if (!RealtimeTiming::published(publication, Clock::now()))
            return;

        coordinator_.enqueue([this, epoch, publication] {
            if (coordinator_.epoch() == epoch && coordinator_.state() == XMAX_GENERATING &&
                timing_.update_publication(publication))
                coordinator_.emit(timing_.snapshot());
        });
    };
    callbacks.frame = [this, epoch](Frame frame) {
        if (coordinator_.epoch() == epoch)
            render_.receive(std::move(frame), epoch);
    };
    callbacks.event = [this, epoch](xmax_event event) {
        if (coordinator_.epoch() != epoch)
            return;
        coordinator_.emit(event);
        if (event.type == XMAX_EVENT_STATISTICS && event.quality >= 0) {
            const double sampled = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
            coordinator_.enqueue([this, epoch, event, sampled] {
                if (coordinator_.epoch() != epoch || coordinator_.state() != XMAX_GENERATING)
                    return;
                for (auto result : stream_.observe_quality(
                         event.direction, event.quality, sampled, coordinator_.cancellation(epoch)
                     ))
                    coordinator_.emit(result);
            });
        }
    };
    callbacks.failure = [this, epoch](Error error) {
        coordinator_.enqueue([this, epoch, error] {
            if (coordinator_.epoch() == epoch && !connection_.active().id.empty())
                fail(error);
        });
    };
    connection_.join(
        stream_, media_.external().format(), microphone || camera_microphone_, std::move(callbacks), cancel,
        MediaService::generation(media_.external().format())
    );
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    render_.bind(epoch);
    input_epoch_ = epoch;
    timing_.begin_publication();
    media_.external().start([this, cancel](const Frame& frame) {
        if (!cancel())
            stream_.push(frame);
    });
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    timing_.finish_join();
    change_state(XMAX_CONNECTED);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * 创建服务端会话并加入 RTC，暂不启动生成任务。
 *
 * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
 */
void XmaxRealtimeManager::connect(
    bool microphone
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, microphone, epoch] {
        require(
            coordinator_.state() == XMAX_READY, XMAX_INVALID_STATE,
            "Media is not ready or a connection already exists"
        );
        try {
            connect_impl(coordinator_.cancellation(epoch), epoch, microphone);
        } catch (...) {
            auto original = std::current_exception();
            try {
                std::rethrow_exception(original);
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(XMAX_INTERNAL_ERROR, "Realtime operation failed"));
            }
            coordinator_.invalidate();
            cleanup(false);
            std::rethrow_exception(original);
        }
    });
}

/**
 * 发送生成条件并接通输入帧交付，等待结果流首帧。
 *
 * @param[in] context 本次生成使用的提示词、参考图片和人脸修复条件。
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 */
void XmaxRealtimeManager::generate_impl(
    const Context& context,
    const Cancel& cancel
) {
    const auto maintain = [this, cancel] {
        if (Clock::now() >= coordinator_.deadline())
            keepalive(cancel);
    };

    if (coordinator_.state() == XMAX_GENERATING) {
        generation_.run(stream_, context, true, cancel, maintain);
        return;
    }

    require(coordinator_.state() == XMAX_CONNECTED, XMAX_INVALID_STATE, "No realtime connection is active");

    timing_.begin_signal();
    render_.trajectory.clear();
    generation_.run(stream_, context, false, cancel, maintain);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
    require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
    timing_.finish_signal();
    coordinator_.emit(timing_.snapshot());
    change_state(XMAX_GENERATING);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * 在需要时建立连接并启动生成，阻塞等待结果首帧。
 *
 * @param[in] prompt UTF-8 生成提示词。
 * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
 * @param[in] face 是否启用生成条件中的人脸修复。
 * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
 */
void XmaxRealtimeManager::start(
    std::string prompt,
    std::string reference,
    bool face,
    bool microphone
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, context = Context{std::move(prompt), std::move(reference), face}, microphone,
                       epoch] {
        const auto cancel = coordinator_.cancellation(epoch);
        require(!cancel(), XMAX_CANCELLED, "Operation cancelled");
        require(
            coordinator_.state() == XMAX_READY || coordinator_.state() == XMAX_CONNECTED ||
                coordinator_.state() == XMAX_GENERATING,
            XMAX_INVALID_STATE, "Prepare local media before generation"
        );

        // 先校验 RTC 能力和 API 凭证，校验失败时保留已就绪的本地媒体。
        if (coordinator_.state() == XMAX_READY) {
            require(!api_->config().key.empty(), XMAX_INVALID_API_KEY, "An Xmax API Key is required");
        }

        if (coordinator_.state() == XMAX_GENERATING) {
            generate_impl(context, cancel);
            return;
        }

        try {
            if (coordinator_.state() == XMAX_READY)
                connect_impl(cancel, epoch, microphone);
            generate_impl(context, cancel);
        } catch (...) {
            auto original = std::current_exception();
            try {
                std::rethrow_exception(original);
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(XMAX_INTERNAL_ERROR, "Realtime operation failed"));
            }
            coordinator_.invalidate();
            cleanup(false);
            std::rethrow_exception(original);
        }
    });
}

/**
 * 使用指定或缓存的生成条件启动任务，阻塞等待结果首帧。
 *
 * @param[in] value 可选生成条件；空指针复用此前条件，首次调用必须提供条件。
 */
void XmaxRealtimeManager::generate(
    const xmax_generation_context* value
) {
    std::optional<Context> incoming;

    if (value)
        incoming = Context{
            value->prompt, value->reference_path ? value->reference_path : "", value->face_restoration != 0
        };

    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, incoming = std::move(incoming), epoch] {
        require(
            incoming.has_value() || generation_.context.has_value(), XMAX_INVALID_ARGUMENT,
            "Initial generation conditions are required"
        );

        const auto context = incoming ? *incoming : *generation_.context;
        require(
            coordinator_.state() == XMAX_READY || coordinator_.state() == XMAX_CONNECTED ||
                coordinator_.state() == XMAX_GENERATING,
            XMAX_INVALID_STATE, "Prepare local media before generation"
        );

        const auto cancel = coordinator_.cancellation(epoch);

        if (coordinator_.state() == XMAX_GENERATING) {
            generate_impl(context, cancel);
            return;
        }

        try {
            if (coordinator_.state() == XMAX_READY)
                connect_impl(cancel, epoch, camera_microphone_);
            generate_impl(context, cancel);
        } catch (...) {
            auto original = std::current_exception();
            try {
                std::rethrow_exception(original);
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(XMAX_INTERNAL_ERROR, "Realtime operation failed"));
            }
            coordinator_.invalidate();
            cleanup(false);
            std::rethrow_exception(original);
        }
    });
}

/**
 * 替换当前任务的生成条件。
 *
 * @param[in] prompt UTF-8 生成提示词。
 * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
 * @param[in] face 是否启用生成条件中的人脸修复。
 */
void XmaxRealtimeManager::update(
    std::string prompt,
    std::string reference,
    bool face
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, context = Context{std::move(prompt), std::move(reference), face}, epoch] {
        require(coordinator_.state() == XMAX_GENERATING, XMAX_INVALID_STATE, "No generation is active");
        generate_impl(context, coordinator_.cancellation(epoch));
    });
}

/**
 * 切换正在使用的摄像头，并使用保留的生成条件恢复任务。
 *
 * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
 */
void XmaxRealtimeManager::switch_camera(
    std::string device
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, device = std::move(device), epoch] {
        const auto state = coordinator_.state();
        require(
            media_.camera_active() &&
                (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
            XMAX_INVALID_STATE, "No switchable camera stream is active"
        );

        const auto cancel = coordinator_.cancellation(epoch);

        try {
            media_.stop();

            if (state == XMAX_GENERATING) {
                stream_.stop_generation(cancel);
                change_state(XMAX_CONNECTED);
            }

            render_.unbind(epoch);
            bind_local();
            media_.switch_camera(device, media_failure(media_epoch_), cancel);

            if (state != XMAX_READY) {
                media_.external().start([this, cancel](const Frame& frame) {
                    if (!cancel())
                        stream_.push(frame);
                });
                render_.bind(epoch);
                if (state == XMAX_GENERATING)
                    generate_impl(*generation_.context, cancel);
            }
        } catch (...) {
            auto original = std::current_exception();
            try {
                std::rethrow_exception(original);
            } catch (const Error& error) {
                errors_.record(error);
            } catch (...) {
                errors_.record(Error(XMAX_INTERNAL_ERROR, "Realtime operation failed"));
            }
            coordinator_.invalidate();
            cleanup(true);
            std::rethrow_exception(original);
        }
    });
}

/**
 * 读取当前会话状态、会话标识和任务标识。
 */
xmax_event XmaxRealtimeManager::state_snapshot() {
    xmax_event event{};
    coordinator_.sync([this, &event] {
        event.struct_size = sizeof(event);
        event.type = XMAX_EVENT_STATE;
        event.state = coordinator_.state();
        text_copy(event.session_id, sizeof(event.session_id), last_session_id_);
        text_copy(event.task_id, sizeof(event.task_id), last_task_id_);
    });
    return event;
}

/**
 * 读取当前绑定的视频输入规格。
 */
xmax_video_format XmaxRealtimeManager::format() {
    xmax_video_format result{};
    coordinator_.sync([this, &result] { result = media_.external().format(); });
    return result;
}

/**
 * 读取当前或下次连接使用的远端播放音量。
 *
 * @return 范围为 [0, 1] 的播放音量，初始值为 0。
 */
float XmaxRealtimeManager::volume() {
    float result = 0;
    coordinator_.sync([this, &result] { result = stream_.volume(); });
    return result;
}

/**
 * 设置帧插值开关；缺少插值后端时拒绝启用。
 *
 * @param[in] enabled true 表示启用，false 表示关闭。
 */
void XmaxRealtimeManager::interpolation(
    bool enabled
) {
    require(!enabled, XMAX_UNSUPPORTED, "This build has no native frame interpolation backend");
}

/**
 * 校验并应用上行码率限制和降级偏好。
 *
 * @param[in] value 上行码率限制与降级偏好，单位和特殊值见 xmax_encoding_config。
 */
void XmaxRealtimeManager::encoding(
    xmax_encoding_config value
) {
    coordinator_.sync([this, value] { stream_.encoding(value); });
}

/**
 * 开启或关闭画质自适应，并重置网络观察窗口。
 *
 * @param[in] enabled true 表示启用，false 表示关闭。
 */
void XmaxRealtimeManager::adaptive(
    bool enabled
) {
    coordinator_.sync([this, enabled] { stream_.adaptive(enabled); });
}

/**
 * 读取当前双向画质档位及生效的视频规格。
 *
 * @return 当前自适应开关、上下行画质档位以及实际使用的视频尺寸和帧率。
 */
xmax_quality_status XmaxRealtimeManager::quality() {
    xmax_quality_status value{};
    coordinator_.sync([this, &value] { value = stream_.quality(); });
    return value;
}

/**
 * 清理 RTC 与服务端会话，并根据选项释放媒体和生成条件。
 *
 * @param[in] close 是否同时释放本地媒体和缓存生成条件。
 * @return 服务端会话已成功关闭时返回 true；关闭请求失败时返回 false 并投递错误事件。
 */
bool XmaxRealtimeManager::cleanup(
    bool close
) {
    generation_.reset(false);
    render_.trajectory.clear();
    media_.stop();
    render_.unbind(coordinator_.epoch());
    coordinator_.deadline(Clock::time_point::max());

    if (!connection_.active().id.empty())
        change_state(XMAX_DISCONNECTING);

    timing_.reset_publication();

    bool server_closed = true;
    const auto session_id = connection_.active().id;

    try {
        connection_.close(stream_);
    } catch (...) {
        server_closed = false;
        Error error(XMAX_API_ERROR, "Server session close failed");
        try {
            throw;
        } catch (const Error& original) {
            error = original;
        } catch (...) {
        }
        if (errors_.reason() != XMAX_REASON_FAILURE) {
            errors_.record(error);
        }
        xmax_event event{};
        event.type = XMAX_EVENT_ERROR;
        event.code = error.code;
        text_copy(event.session_id, sizeof(event.session_id), session_id);
        text_copy(event.message, sizeof(event.message), XmaxLogger::redact(error.what()));
        coordinator_.emit(event, XMAX_REASON_FAILURE, &error);
    }

    if (close) {
        media_.close();
        render_.unbind_local(++media_epoch_);
        camera_microphone_ = false;
        generation_.context.reset();
    }

    change_state(media_.external().bound() ? XMAX_READY : XMAX_IDLE);

    return server_closed;
}

/**
 * 取消在途操作并断开连接，可选择同时关闭本地媒体。
 *
 * @param[in] close 是否同时释放本地媒体和缓存生成条件。
 */
void XmaxRealtimeManager::disconnect(
    bool close
) {
    const auto epoch = coordinator_.invalidate();
    media_.stop();
    render_.unbind(epoch);
    coordinator_.sync([this, close] {
        errors_.reset();
        if (!cleanup(close))
            throw Error(
                XMAX_API_ERROR, "Server session close failed; inspect the session ID in the error event",
                errors_.error() ? errors_.error()->api_code : 0,
                errors_.error() ? errors_.error()->http_status : 0
            );
    });
}

/**
 * 清理失败操作持有的资源，并投递原始 SDK 错误。
 *
 * @param[in] error 需要保留错误码与诊断的原始 SDK 异常。
 */
void XmaxRealtimeManager::fail(
    Error error
) {
    coordinator_.invalidate();
    errors_.record(error);
    cleanup(false);

    xmax_event event{};
    event.type = XMAX_EVENT_ERROR;
    event.code = error.code;
    event.state = coordinator_.state();
    text_copy(event.message, sizeof(event.message), XmaxLogger::redact(error.what()));
    coordinator_.emit(event, XMAX_REASON_FAILURE, &error);
}

/**
 * 为活动连接执行到期的服务端与房间保活。
 */
void XmaxRealtimeManager::heartbeat() {
    const auto cancel = coordinator_.cancellation(coordinator_.epoch());
    try {
        keepalive(cancel);
    } catch (const Error& error) {
        if (error.code != XMAX_CANCELLED)
            fail(error);
        else
            coordinator_.deadline(Clock::time_point::max());
    } catch (const std::exception&) {
        fail(Error(XMAX_API_ERROR, "Session heartbeat failed"));
    }
}

/**
 * 在生成等待期间检查取消，并维持服务端和房间活跃。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 */
void XmaxRealtimeManager::keepalive(
    const Cancel& cancel
) {
    connection_.heartbeat(stream_, cancel);
    coordinator_.deadline(Clock::now() + heartbeat_interval_);
}

/**
 * 设置并保存当前或下次连接使用的远端播放音量。
 *
 * @param[in] value 范围为 [0, 1] 的有限音量值，0 表示静音。
 */
void XmaxRealtimeManager::volume(
    float value
) {
    coordinator_.sync([this, value] { stream_.volume(value); });
}

/**
 * 更新已就绪且未连接的摄像头编码规格，失败时尝试恢复原规格。
 *
 * @param[in] format 模型支持的目标尺寸和帧率。
 */
void XmaxRealtimeManager::update_camera_format(
    xmax_video_format format
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, format, epoch] {
        require(
            coordinator_.state() == XMAX_READY && media_.camera_active(), XMAX_INVALID_STATE,
            "Camera format can only be updated before connection"
        );
        const auto resolved = MediaService::camera_input(format);
        errors_.reset();
        change_state(XMAX_PREPARING);
        try {
            media_.update_camera_format(
                resolved, media_failure(media_epoch_), coordinator_.cancellation(epoch)
            );
            change_state(XMAX_READY);
        } catch (const Error& error) {
            errors_.record(error);
            change_state(media_.camera_active() ? XMAX_READY : XMAX_IDLE);
            throw;
        }
    });
}

/**
 * 读取当前摄像头规格，没有摄像头流时返回全零。
 */
xmax_video_format XmaxRealtimeManager::camera_format() {
    xmax_video_format result{};
    coordinator_.sync([this, &result] {
        if (media_.camera_active())
            result = media_.external().format();
    });
    return result;
}

/**
 * 在生成中提交指定方向的档位，不锁定后续自动调档。
 *
 * @param[in] direction 0 为上行，1 为下行。
 * @param[in] level 目标档位，范围为 [1, 5]。
 */
void XmaxRealtimeManager::quality_level(
    int direction,
    int level
) {
    const auto epoch = coordinator_.epoch();
    coordinator_.sync([this, direction, level, epoch] {
        require(
            coordinator_.state() == XMAX_GENERATING, XMAX_INVALID_STATE,
            "Quality levels can only be changed during generation"
        );
        const auto cancel = coordinator_.cancellation(epoch);
        coordinator_.emit(stream_.set_quality(direction, level, cancel, [this, cancel] {
            if (Clock::now() >= coordinator_.deadline())
                keepalive(cancel);
        }));
    });
}

/**
 * 读取包含原因和错误的当前状态快照。
 */
xmax_event_info XmaxRealtimeManager::state_info() {
    xmax_event_info info{};
    coordinator_.sync([this, &info] {
        info.struct_size = sizeof(info);
        info.event.struct_size = sizeof(info.event);
        info.event.type = XMAX_EVENT_STATE;
        info.event.state = coordinator_.state();
        info.event.code = errors_.error() ? errors_.error()->code : XMAX_OK;
        text_copy(info.event.session_id, sizeof(info.event.session_id), last_session_id_);
        text_copy(info.event.task_id, sizeof(info.event.task_id), last_task_id_);
        info.reason = errors_.reason();
        info.error = errors_.details();
    });
    return info;
}

/**
 * 按业务原因断开连接，保留本地媒体。
 *
 * @param[in] reason 正常停止或方向变化原因。
 */
void XmaxRealtimeManager::disconnect_reason(
    xmax_realtime_reason reason
) {
    require(
        reason == XMAX_REASON_NORMAL || reason == XMAX_REASON_ORIENTATION_CHANGED, XMAX_INVALID_ARGUMENT,
        "Failure reasons are reported by the SDK"
    );
    const auto epoch = coordinator_.invalidate();
    media_.stop();
    render_.unbind(epoch);
    coordinator_.sync([this, reason] {
        errors_.reset(reason);
        if (!cleanup(false))
            throw Error(
                XMAX_API_ERROR, "Server session close failed",
                errors_.error() ? errors_.error()->api_code : 0,
                errors_.error() ? errors_.error()->http_status : 0
            );
    });
}

/**
 * 复制视口轨迹并预约异步发送，连续提交仅保留最新采样。
 *
 * @param[in] frame 采样点和实际视频布局。
 */
void XmaxRealtimeManager::submit_interaction(
    const xmax_interaction_frame& frame
) {
    auto points = InteractionCoordinateMapper::map(frame);
    if (points.empty())
        return;
    const auto epoch = coordinator_.epoch();
    std::lock_guard<std::mutex> lock(interaction_mutex_);
    require(
        !interaction_task_.empty() && coordinator_.state() == XMAX_GENERATING, XMAX_INVALID_STATE,
        "No generation is active"
    );
    render_.trajectory.update(points);
    if (generation_.interaction.submit({epoch, interaction_task_, std::move(points)}))
        coordinator_.enqueue([this] { drain_interaction(); });
}

/**
 * 公平调度单个轨迹采样，避免连续鼠标输入占满生命周期执行器。
 */
void XmaxRealtimeManager::drain_interaction() {
    auto sample = generation_.interaction.take();
    if (!sample)
        return;
    const auto epoch = coordinator_.epoch();
    const auto cancel = coordinator_.cancellation(epoch);
    try {
        if (coordinator_.state() == XMAX_GENERATING)
            generation_.send_interaction(stream_, *sample, epoch, cancel, [this, cancel] {
                if (Clock::now() >= coordinator_.deadline())
                    keepalive(cancel);
            });
    } catch (const Error& error) {
        if (error.code != XMAX_CANCELLED) {
            xmax_event event{};
            event.type = XMAX_EVENT_ERROR;
            event.code = error.code;
            event.state = coordinator_.state();
            text_copy(event.message, sizeof(event.message), XmaxLogger::redact(error.what()));
            coordinator_.emit(event, XMAX_REASON_FAILURE, &error);
        }
    }
    coordinator_.enqueue([this] { drain_interaction(); });
}
} // namespace xmax::detail
