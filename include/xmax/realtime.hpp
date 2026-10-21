//
//  realtime.hpp
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
#include "frame.hpp"
#include "RealtimeCameraPreset.hpp"
#include "RealtimeQualityLevel.hpp"
#include <string>
#include <vector>

namespace xmax {

/**
 * 管理媒体输入、RTC 连接和生成任务；耗时操作应在工作线程调用。
 */
class realtime : public detail::owner<xmax_realtime, xmax_realtime_destroy> {

  public:

    /**
     * 继承句柄所有者的构造接口，接管传入句柄；空句柄表示空对象。
     *
     * @note 不复制底层资源，销毁时调用与句柄匹配的释放接口。
     */
    using owner::owner;

    /**
     * 绑定由接入方提供视频帧的外部输入。
     *
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_prepare_external
     */
    void prepare(
        xmax_video_format format
    ) {
        check(xmax_realtime_prepare_external(get(), &format));
    }

    /**
     * 启动 SDK 摄像头采集并等待第一张真实帧。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_prepare_camera
     */
    void prepare_camera(
        const std::string& device = {},
        xmax_video_format format = {},
        bool microphone = false
    ) {
        const xmax_camera_config config{
            sizeof(config), device.empty() ? nullptr : device.c_str(), format, microphone
        };
        check(xmax_realtime_prepare_camera(get(), &config));
    }

    /**
     * 切换活动摄像头，保留当前连接与生成条件。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_switch_camera
     */
    void switch_camera(
        const std::string& device
    ) {
        check(xmax_realtime_switch_camera(get(), device.c_str()));
    }

    /**
     * 在断开连接后释放本地采集和输入绑定。
     *
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_stop_media
     */
    void stop_media() {
        check(xmax_realtime_stop_media(get()));
    }

    /**
     * 创建服务端会话并加入 RTC，暂不启动生成。
     *
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_connect
     */
    void connect(
        bool microphone = false
    ) {
        check(xmax_realtime_connect(get(), microphone));
    }

    /**
     * 连接并启动生成，阻塞等待结果首帧。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_start
     */
    void start(
        const std::string& prompt,
        const std::string& reference = {},
        bool microphone = false,
        bool face = false
    ) {
        const xmax_generation_context context{
            sizeof(context), prompt.c_str(), reference.empty() ? nullptr : reference.c_str(), face
        };
        check(xmax_realtime_start(get(), &context, microphone));
    }

    /**
     * 在已有连接上使用缓存条件启动生成。
     *
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_generate
     */
    void generate() {
        check(xmax_realtime_generate(get(), nullptr));
    }

    /**
     * 在已有连接上使用指定条件启动生成。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_generate
     */
    void generate(
        const std::string& prompt,
        const std::string& reference = {},
        bool face = false
    ) {
        const xmax_generation_context context{
            sizeof(context), prompt.c_str(), reference.empty() ? nullptr : reference.c_str(), face
        };
        check(xmax_realtime_generate(get(), &context));
    }

    /**
     * 替换活动任务的生成条件。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_update
     */
    void update(
        const std::string& prompt,
        const std::string& reference = {},
        bool face = false
    ) {
        const xmax_generation_context context{
            sizeof(context), prompt.c_str(), reference.empty() ? nullptr : reference.c_str(), face
        };
        check(xmax_realtime_update(get(), &context));
    }

    /**
     * 检查当前连接是否请求接入方提交外部视频帧。
     *
     * @return 当前需要外部输入帧时为 true；SDK 摄像头模式返回 false。
     * @see xmax_realtime_is_requesting_frames
     */
    bool requesting_frames() const {
        int32_t result = 0;
        check(xmax_realtime_is_requesting_frames(get(), &result));
        return result != 0;
    }

    /**
     * 同步复制并提交一帧外部视频，返回后接入方可复用原像素内存。
     *
     * @param[in] value 带 struct_size 的外部视频视图，尺寸须匹配绑定规格且时间戳严格递增。
     * @see xmax_realtime_push_video_frame
     */
    void push(
        const xmax_video_frame& value
    ) {
        check(xmax_realtime_push_video_frame(get(), &value));
    }

    /**
     * 设置并保存当前或下次连接使用的远端播放音量。
     *
     * @param[in] value 范围为 [0, 1] 的有限音量值，0 表示静音。
     * @see xmax_realtime_set_remote_volume
     */
    void volume(
        float value
    ) {
        check(xmax_realtime_set_remote_volume(get(), value));
    }

    /**
     * 读取当前或下次连接使用的远端播放音量。
     *
     * @return 范围为 [0, 1] 的播放音量，初始值为 0。
     * @see xmax_realtime_get_remote_volume
     */
    float volume() const {
        float value = 0;
        check(xmax_realtime_get_remote_volume(get(), &value));
        return value;
    }

    /**
     * 读取当前状态快照，不消费待处理事件。
     *
     * @return 包含当前状态与会话标识的快照事件。
     * @see xmax_realtime_get_state
     */
    xmax_event state() const {
        xmax_event value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_state(get(), &value));
        return value;
    }

    /**
     * 读取当前绑定的视频输入规格。
     *
     * @see xmax_realtime_get_format
     */
    xmax_video_format format() const {
        xmax_video_format value{};
        check(xmax_realtime_get_format(get(), &value));
        return value;
    }

    /**
     * 非阻塞消费下一个应用事件。
     *
     * @param[out] event 接收最早的待处理应用事件，本方法自动设置 struct_size。
     * @return 取得事件返回 true；当前无事件返回 false。
     * @see xmax_realtime_poll_event
     */
    bool poll(
        xmax_event& event
    ) const {
        event.struct_size = sizeof(event);
        const auto result = xmax_realtime_poll_event(get(), &event);
        check(result);
        return result == XMAX_OK;
    }

    /**
     * 非阻塞取出最新的远端结果帧，并转移该帧的所有权。
     *
     * @return 返回的帧持有像素数据；没有新帧时返回空对象。
     * @see xmax_realtime_acquire_frame
     */
    frame acquire_frame() const {
        xmax_frame value = nullptr;
        check(xmax_realtime_acquire_frame(get(), &value));
        return frame(value);
    }

    /**
     * 非阻塞取出最新的本地预览帧，并转移该帧的所有权。
     *
     * @return 返回的帧持有像素数据；没有新帧时返回空对象。
     * @see xmax_realtime_acquire_local_frame
     */
    frame acquire_local_frame() const {
        xmax_frame value = nullptr;
        check(xmax_realtime_acquire_local_frame(get(), &value));
        return frame(value);
    }

    /**
     * 设置上行码率限制及画质或帧率的降级偏好。
     *
     * @param[in] minimum 最小码率，单位为 kbps；-1 使用策略默认值，0 不设置最小限制。
     * @param[in] maximum 最大码率，单位为 kbps；0 使用策略估计值。
     * @param[in] preference 使用 XMAX_ENCODER_* 表示的编码降级偏好。
     * @see xmax_realtime_set_encoding
     */
    void encoding(
        int minimum = -1,
        int maximum = 0,
        int preference = XMAX_ENCODER_MAINTAIN_FRAMERATE
    ) {
        const xmax_encoding_config value{sizeof(value), minimum, maximum, preference};
        check(xmax_realtime_set_encoding(get(), &value));
    }

    /**
     * 开启或关闭独立的双向画质自适应。
     *
     * @param[in] enabled true 表示启用，false 表示关闭。
     * @see xmax_realtime_set_adaptive_quality
     */
    void adaptive_quality(
        bool enabled
    ) {
        check(xmax_realtime_set_adaptive_quality(get(), enabled));
    }

    /**
     * 读取当前双向画质档位及生效的视频规格。
     *
     * @return 当前自适应开关、上下行画质档位以及实际使用的视频尺寸和帧率。
     * @see xmax_realtime_get_quality
     */
    xmax_quality_status quality() const {
        xmax_quality_status value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_quality(get(), &value));
        return value;
    }

    /**
     * 设置原生帧插值开关；缺少后端时拒绝启用。
     *
     * @param[in] enabled true 表示启用，false 表示关闭。
     * @see xmax_realtime_set_interpolation
     */
    void interpolation(
        bool enabled
    ) {
        check(xmax_realtime_set_interpolation(get(), enabled));
    }

    /**
     * 关闭服务端和 RTC 资源，保留本地预览与缓存生成条件。
     *
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_disconnect
     */
    void disconnect() {
        check(xmax_realtime_disconnect(get()));
    }

    /**
     * 断开连接并释放本地媒体和缓存生成条件。
     *
     * @note 操作会阻塞，应在工作线程调用；失败时抛出 xmax::error。
     * @see xmax_realtime_close
     */
    void close() {
        check(xmax_realtime_close(get()));
    }

    /**
     * 更新已就绪且未连接的摄像头编码规格，保留设备和预览绑定。
     *
     * @param[in] format 模型支持的新规格。
     */
    void update_camera_format(
        xmax_video_format format
    ) {
        check(xmax_realtime_update_camera_format(get(), &format));
    }

    /**
     * 更新摄像头分辨率，沿用当前帧率、码率和编码偏好。
     *
     * @param[in] preset 模型支持的摄像头分辨率。
     */
    void update_camera_format(
        RealtimeCameraPreset preset
    ) {
        check(xmax_realtime_update_camera_preset(get(), static_cast<xmax_camera_preset>(preset)));
    }

    /**
     * 获取摄像头编码规格；无摄像头流时返回全零。
     */
    xmax_video_format camera_format() const {
        xmax_video_format result{};
        xmax_camera_preset preset{};
        check(xmax_realtime_get_camera_format(get(), &result, &preset));
        return result;
    }

    /**
     * 获取当前摄像头预设；无摄像头流时返回 none。
     */
    RealtimeCameraPreset camera_preset() const {
        xmax_video_format format{};
        xmax_camera_preset result{};
        check(xmax_realtime_get_camera_format(get(), &format, &result));
        return static_cast<RealtimeCameraPreset>(result);
    }

    /**
     * 在生成中调整上行档位，后续网络自适应仍可继续调档。
     *
     * @param[in] level 目标上行档位。
     */
    void uplink_quality_level(
        RealtimeQualityLevel level
    ) {
        check(xmax_realtime_set_uplink_quality_level(get(), static_cast<int32_t>(level)));
    }

    /**
     * 在生成中调整下行档位，发送失败时保持此前档位。
     *
     * @param[in] level 目标下行档位。
     */
    void downlink_quality_level(
        RealtimeQualityLevel level
    ) {
        check(xmax_realtime_set_downlink_quality_level(get(), static_cast<int32_t>(level)));
    }

    /**
     * 获取包含状态原因与错误详情的状态快照。
     */
    xmax_event_info state_info() const {
        xmax_event_info info{};
        info.struct_size = sizeof(info);
        check(xmax_realtime_get_state_info(get(), &info));
        return info;
    }

    /**
     * 非阻塞取出包含原因与错误详情的事件，与旧 poll 接口共用事件队列。
     *
     * @param[out] info 接收最早事件，队列为空时不变。
     * @return 是否取得事件。
     */
    bool poll(
        xmax_event_info& info
    ) {
        info.struct_size = sizeof(info);
        const auto result = xmax_realtime_poll_event_info(get(), &info);
        check(result);
        return result == XMAX_OK;
    }

    /**
     * 按业务原因断开连接，保留本地媒体预览。
     *
     * @param[in] reason 正常停止或显示方向变化。
     */
    void disconnect(
        xmax_realtime_reason reason
    ) {
        check(xmax_realtime_disconnect_with_reason(get(), reason));
    }

    /**
     * 提交轨迹采样，由 SDK 完成坐标映射和异步信令发送。
     *
     * @param[in] frame 采样点、视口尺寸和实际视频布局。
     */
    void submit_interaction(
        const xmax_interaction_frame& frame
    ) {
        check(xmax_realtime_submit_interaction(get(), &frame));
    }

    /**
     * 获取尚未过期的归一化轨迹副本，供界面自定义绘制。
     */
    std::vector<xmax_point> trajectory() const {
        std::vector<xmax_point> points(256);
        size_t count = 0;
        check(xmax_realtime_get_trajectory(get(), points.data(), points.size(), &count));
        points.resize(count);
        return points;
    }

    /**
     * 在 HWND 上显示帧并绘制默认轨迹效果，须在窗口所属线程调用。
     *
     * @param[in] value 待显示的视频帧。
     * @param[in] window 当前线程拥有的 HWND。
     * @param[in] content_mode 等比例适应或居中填满。
     */
    void render_window(
        xmax_frame value,
        void* window,
        int32_t content_mode = XMAX_CONTENT_FIT
    ) {
        check(xmax_realtime_render_window(get(), value, window, content_mode));
    }
};
} // namespace xmax
