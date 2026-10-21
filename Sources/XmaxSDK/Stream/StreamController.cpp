//
//  StreamController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/StreamController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <cmath>

namespace xmax::detail {

/**
 * 离开 RTC 房间并排空内部回调，释放发布与信令资源。
 */
StreamController::~StreamController() {
    stop();
}

/**
 * 在分配服务端会话前创建原生 RTC 适配器。
 */
void StreamController::allocate() {
    room_.allocate();
}

/**
 * 按输入编码和音频策略加入房间，并安装受生命周期保护的回调。
 *
 * @param[in] connection 当前 RTC 房间连接信息。
 * @param[in] format 请求的视频像素宽高及每秒帧数，须符合调用接口的规格限制。
 * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
 * @param[in] callbacks 接收结果帧、RTC 事件、错误和首次发布通知的回调；停止后不再触发。
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] generation_format 服务端生成画布；宽高为 0 时使用输入规格。
 */
void StreamController::join(
    const Connection& connection,
    xmax_video_format format,
    bool microphone,
    RtcCallbacks callbacks,
    const Cancel& cancel,
    xmax_video_format generation_format
) {
    format_ = format;
    generation_format_ = generation_format.width ? generation_format : format;

    if (history_) {
        quality_.up.level = history_->read(false);
        quality_.down.level = history_->read(true);
    } else {
        if (Clock::now() - quality_saved_[0] >= std::chrono::hours(1))
            quality_.up.level = 2;
        if (Clock::now() - quality_saved_[1] >= std::chrono::hours(1))
            quality_.down.level = 2;
    }

    quality_.reset_samples();
    // 在加入房间前设置播放策略，确保订阅结果用户时立即使用正确音量。
    require(room_.backend() != nullptr, XMAX_INVALID_STATE, "Allocate an RTC backend before joining");
    room_.backend()->volume(volume_);
    room_.join(connection, uplink_encoding(quality_.up.level), microphone, std::move(callbacks), cancel);
    XmaxLogger::write(LogCategory::Stream, XMAX_LOG_INFO, [&](auto language) {
        return XmaxLogger::localized(language, "RTC 已连接 (RTC Connected)", "RTC Connected") + "\n└─ " +
               std::to_string(format.width) + " × " + std::to_string(format.height) + " @ " +
               std::to_string(format.fps) + " fps, microphone=" + (microphone ? "on" : "off");
    });
}

/**
 * 创建生成任务并发送启动信令，等待期间执行必要的维护。
 *
 * @param[in] prompt UTF-8 生成提示词。
 * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
 * @param[in] face 是否启用生成条件中的人脸修复。
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] maintain 等待或限速期间执行的维护回调，空函数表示无需额外维护。
 */
void StreamController::start(
    const std::string& prompt,
    const std::string& reference,
    bool face,
    const Cancel& cancel,
    const std::function<void()>& maintain
) {
    room_.task = task_id();
    require(room_.joined(), XMAX_INVALID_STATE, "No RTC room is connected");
    room_.backend()->begin_generation();
    room_.send(generation_message("start", prompt, reference, face), cancel, maintain);
}

/**
 * 等待真实结果首帧，同时检查取消并执行保活。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] keepalive 等待期间周期执行的保活函数，不得持有跨线程生命周期锁。
 */
void StreamController::wait_remote(
    const Cancel& cancel,
    const std::function<void()>& keepalive
) {
    room_.backend()->wait_remote(cancel, keepalive);
}

/**
 * 使用当前任务标识发送替换后的生成条件。
 *
 * @param[in] prompt UTF-8 生成提示词。
 * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
 * @param[in] face 是否启用生成条件中的人脸修复。
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @param[in] maintain 等待或限速期间执行的维护回调，空函数表示无需额外维护。
 */
void StreamController::update(
    const std::string& prompt,
    const std::string& reference,
    bool face,
    const Cancel& cancel,
    const std::function<void()>& maintain
) {
    require(room_.joined() && !room_.task.empty(), XMAX_INVALID_STATE, "No generation is active");
    room_.send(generation_message("change_condition", prompt, reference, face), cancel, maintain);
}

/**
 * 为当前生成任务发送房间保活信令。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 */
void StreamController::heartbeat(
    const Cancel& cancel
) {
    require(room_.joined(), XMAX_INVALID_STATE, "No RTC room is connected");
    room_.send(room_event("heartbeat", room_.connection), cancel);
}

/**
 * 停止当前生成任务，保留 RTC 房间连接。
 *
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 */
void StreamController::stop_generation(
    const Cancel& cancel
) {
    require(room_.joined(), XMAX_INVALID_STATE, "No RTC room is connected");
    if (!room_.task.empty())
        room_.send(room_event("stop", room_.connection, room_.task), cancel);
    room_.task.clear();
}

/**
 * 解除媒体线程的 RTC 引用，离开房间并清除信令状态。
 */
void StreamController::stop() noexcept {
    if (room_.joined() && !room_.task.empty()) {
        try {
            room_.send(room_event("stop", room_.connection, room_.task), [] { return false; });
        } catch (...) {
        }
    }
    room_.leave();
    quality_.reset_samples();
}

/**
 * 通过活动 RTC 适配器的线程安全快照发布一帧视频。
 *
 * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
 */
void StreamController::push(
    const Frame& frame
) {
    room_.push(frame);
}

/**
 * 设置并保存当前或下次连接使用的远端播放音量。
 *
 * @param[in] value 范围为 [0, 1] 的有限音量值，0 表示静音。
 */
void StreamController::volume(
    float value
) {
    require(
        std::isfinite(value) && value >= 0 && value <= 1, XMAX_INVALID_ARGUMENT,
        "Volume must be between 0 and 1"
    );
    if (room_.backend())
        room_.backend()->volume(value);
    volume_ = value;
}

/**
 * 将当前档位和码率覆盖配置解析为原生编码参数。
 *
 * @param[in] level 画质档位，范围为 [1, 5]。
 */
VideoEncodingConfiguration StreamController::uplink_encoding(
    int level
) const {
    return encoding_.resolve(format_, level);
}

/**
 * 校验并应用上行码率限制和降级偏好。
 *
 * @param[in] value 上行码率限制与降级偏好，单位和特殊值见 xmax_encoding_config。
 */
void StreamController::encoding(
    xmax_encoding_config value
) {
    encoding_.configure(value, format_, quality_.up.level, room_.joined() ? room_.backend() : nullptr);
}

/**
 * 读取当前双向画质档位及生效的视频规格。
 *
 * @return 当前自适应开关、上下行画质档位以及实际使用的视频尺寸和帧率。
 */
xmax_quality_status StreamController::quality() const {
    xmax_quality_status value{};
    value.struct_size = sizeof(value);
    value.adaptive_enabled = quality_.enabled;
    value.uplink_level = room_.joined() ? quality_.up.level : 0;
    value.downlink_level = room_.joined() ? quality_.down.level : 0;

    if (value.uplink_level) {
        value.uplink = quality_format(format_, quality_.up.level, false);
        value.downlink = quality_format(format_, quality_.down.level, true);
    }

    return value;
}

/**
 * 使用活动房间、任务标识和生成条件构造房间信令。
 *
 * @param[in] event 房间信令的事件名称。
 * @param[in] prompt UTF-8 生成提示词。
 * @param[in] reference 可选的生成参考图片路径或地址。
 * @param[in] face 是否启用生成条件中的人脸修复。
 * @return 可供房间信令编码和发送的生成事件 JSON。
 */
json StreamController::generation_message(
    const std::string& event,
    const std::string& prompt,
    const std::string& reference,
    bool face
) const {
    auto value =
        generation_event(event, room_.connection, room_.task, generation_format_, prompt, reference, face);
    const auto target = quality_format(format_, quality_.down.level, true);
    value["params"]["target_size"] = json::array({target.width, target.height});

    if (event == "start")
        value["params"]["max_bitrate"] = std::min(10000, bitrate(target).second);

    return value;
}

/**
 * 更新指定方向的网络质量策略，返回由此产生的质量或性能事件。
 *
 * @param[in] direction 传输方向，0 为上行，1 为下行。
 * @param[in] network_quality 当前方向的原生网络质量枚举值。
 * @param[in] sampled_at 网络样本产生时的单调秒时间，过期或重复样本不参与决策。
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 * @return 本次观察触发的事件集合；无变更时为空。
 */
std::vector<xmax_event> StreamController::observe_quality(
    int direction,
    int network_quality,
    double sampled_at,
    const Cancel& cancel
) {
    if (!quality_.enabled || !room_.joined() || room_.task.empty() || direction < 0 || direction > 1)
        return {};

    auto& policy = direction ? quality_.down : quality_.up;
    const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    auto proposal = policy.observe(network_quality, sampled_at, now);

    if (!proposal)
        return {};

    try {
        return {set_quality(direction, *proposal, cancel)};
    } catch (const Error& error) {
        policy.complete(*proposal, false, now);
        const auto previous = quality_format(format_, policy.level, direction != 0);
        xmax_event event{};
        event.type = XMAX_EVENT_QUALITY_CHANGE;
        event.code = error.code;
        event.direction = direction;
        event.quality = policy.level;
        event.width = int(previous.width);
        event.height = int(previous.height);
        event.fps = int(previous.fps);
        return {event};
    }
}

/**
 * 应用接入方指定的单向档位，失败时保持旧档位。
 *
 * @param[in] direction 0 为上行，1 为下行。
 * @param[in] level 目标档位，范围为 [1, 5]。
 * @param[in] cancel 协作式取消判断。
 * @param[in] maintain 信令等待期间执行的保活回调。
 * @return 成功提交后的画质变更事件。
 */
xmax_event StreamController::set_quality(
    int direction,
    int level,
    const Cancel& cancel,
    const std::function<void()>& maintain
) {
    require(
        (direction == 0 || direction == 1) && level >= 1 && level <= 5, XMAX_INVALID_ARGUMENT,
        "Invalid quality direction or level"
    );
    require(room_.joined() && !room_.task.empty(), XMAX_INVALID_STATE, "No generation is active");
    require(!cancel(), XMAX_CANCELLED, "Quality adjustment cancelled");
    auto& policy = direction ? quality_.down : quality_.up;
    const auto next = quality_format(format_, level, direction != 0);
    if (level != policy.level) {
        if (direction) {
            auto message = room_event("change_target_size", room_.connection, room_.task);
            message["params"] = {
                {"target_size", json::array({next.width, next.height})},
                {"min_bitrate", 100},
                {"max_bitrate", std::min(10000, bitrate(next).second)}
            };
            room_.send(message, cancel, maintain);
        } else {
            room_.backend()->configure_encoding(uplink_encoding(level));
        }
        policy.complete(level, true, std::chrono::duration<double>(Clock::now().time_since_epoch()).count());
        quality_saved_[direction] = Clock::now();
        if (history_)
            history_->write(level, direction != 0);
    }
    xmax_event event{};
    event.type = XMAX_EVENT_QUALITY_CHANGE;
    event.direction = direction;
    event.quality = level;
    event.width = int(next.width);
    event.height = int(next.height);
    event.fps = int(next.fps);
    event.bitrate_kbps = event.packet_loss_percent = event.e2e_ms = -1;
    return event;
}

} // namespace xmax::detail
