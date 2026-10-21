//
//  XmaxRealtimeManager.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/RealtimeCoordinator.hpp"
#include "Core/Realtime/RealtimeTiming.hpp"
#include "Core/Realtime/RealtimeErrorHandler.hpp"
#include "Media/MediaController.hpp"
#include "Render/RenderController.hpp"
#include "Core/Realtime/XmaxRealtimeConnectionManager.hpp"
#include "Core/Realtime/XmaxRealtimeGenerationManager.hpp"
#include "Stream/StreamController.hpp"
#include <optional>

namespace xmax::detail {

/**
 * 管理媒体绑定、RTC 连接和视频生成生命周期；耗时操作在串行执行器中完成。
 */
class XmaxRealtimeManager {

  public:

    /**
     * 使用共享 API 服务及原生工厂构建独立的实时生命周期控制器。
     *
     * @param[in] api 共享 API 服务，控制器在其生命周期内保留该引用。
     * @param[in] factory 创建底层原生适配器的工厂，不得返回空对象。
     * @param[in] heartbeat 活动会话心跳间隔，单位为毫秒。
     * @param[in] camera 创建原生摄像头适配器的工厂。
     * @param[in] history 可选的按模型和方向保存的画质历史，空引用表示不持久化。
     */
    explicit XmaxRealtimeManager(
        std::shared_ptr<ApiService> api,
        RtcFactory factory = make_rtc,
        std::chrono::milliseconds heartbeat = std::chrono::seconds(10),
        CameraFactory camera = make_camera_capture,
        std::shared_ptr<QualityHistory> history = {}
    );

    /**
     * 取消在途操作，关闭媒体与 RTC 资源并停止生命周期执行器。
     */
    ~XmaxRealtimeManager();

    /**
     * 绑定外部视频输入，并进入媒体就绪状态。
     *
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     */
    void prepare(
        xmax_video_format format
    );

    /**
     * 在需要时建立连接并启动生成，阻塞等待结果首帧。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     */
    void start(
        std::string prompt,
        std::string reference,
        bool face,
        bool microphone
    );

    /**
     * 替换当前任务的生成条件。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] face 是否启用生成条件中的人脸修复。
     */
    void update(
        std::string prompt,
        std::string reference,
        bool face
    );

    /**
     * 取消在途操作并断开连接，可选择同时关闭本地媒体。
     *
     * @param[in] close 是否同时释放本地媒体和缓存生成条件。
     */
    void disconnect(
        bool close
    );

    /**
     * 同步复制并提交外部输入帧，允许从媒体线程调用。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     */
    void push(
        const xmax_video_frame& frame
    );

    /**
     * 启动指定摄像头并等待真实首帧，保留实际采集尺寸并进入就绪状态。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] format 输入尺寸和帧率，支持范围见 xmax_video_format。
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     */
    void prepare_camera(
        std::string device,
        xmax_video_format format,
        bool microphone
    );

    /**
     * 切换正在使用的摄像头，并使用保留的生成条件恢复任务。
     *
     * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
     */
    void switch_camera(
        std::string device
    );

    /**
     * 在断开连接后释放本地采集和输入绑定。
     */
    void stop_media();

    /**
     * 创建服务端会话并加入 RTC，暂不启动生成任务。
     *
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     */
    void connect(
        bool microphone
    );

    /**
     * 使用指定或缓存的生成条件启动任务，阻塞等待结果首帧。
     *
     * @param[in] value 可选生成条件；空指针复用此前条件，首次调用必须提供条件。
     */
    void generate(
        const xmax_generation_context* value
    );

    /**
     * 读取当前会话状态、会话标识和任务标识。
     */
    xmax_event state_snapshot();

    /**
     * 读取当前绑定的视频输入规格。
     */
    xmax_video_format format();

    /**
     * 读取当前或下次连接使用的远端播放音量。
     *
     * @return 范围为 [0, 1] 的播放音量，初始值为 0。
     */
    float volume();

    /**
     * 设置帧插值开关；缺少插值后端时拒绝启用。
     *
     * @param[in] enabled true 表示启用，false 表示关闭。
     */
    void interpolation(
        bool enabled
    );

    /**
     * 校验并应用上行码率限制和降级偏好。
     *
     * @param[in] value 上行码率限制与降级偏好，单位和特殊值见 xmax_encoding_config。
     */
    void encoding(
        xmax_encoding_config value
    );

    /**
     * 开启或关闭画质自适应，并重置网络观察窗口。
     *
     * @param[in] enabled true 表示启用，false 表示关闭。
     */
    void adaptive(
        bool enabled
    );

    /**
     * 读取当前双向画质档位及生效的视频规格。
     *
     * @return 当前自适应开关、上下行画质档位以及实际使用的视频尺寸和帧率。
     */
    xmax_quality_status quality();

    /**
     * 非阻塞检查当前外部输入是否请求供帧。
     *
     * @return 外部输入正在请求供帧时为 true；摄像头模式或未请求供帧时为 false。
     */
    bool requesting() const;

    /**
     * 设置并保存当前或下次连接使用的远端播放音量。
     *
     * @param[in] value 范围为 [0, 1] 的有限音量值，0 表示静音。
     */
    void volume(
        float value
    );

    /**
     * 非阻塞消费最早的待处理应用事件。
     *
     * @param[out] event 接收最早的待处理应用事件，队列为空时保持原值。
     * @return 取得事件返回 true；当前无事件返回 false。
     */
    bool poll(
        xmax_event& event
    ) {
        return coordinator_.poll(event);
    }

    /**
     * 非阻塞取出最新的远端结果帧，并转移该帧的所有权。
     *
     * @return 返回的帧持有像素数据；没有新帧时返回空对象。
     */
    std::shared_ptr<Frame> acquire_frame() {
        return render_.acquire_frame();
    }

    /**
     * 非阻塞取出最新的本地预览帧，并转移该帧的所有权。
     *
     * @return 返回的帧持有像素数据；没有新帧时返回空对象。
     */
    std::shared_ptr<Frame> acquire_local_frame() {
        return render_.acquire_local_frame();
    }

    /**
     * 更新已就绪且未连接的摄像头编码规格，失败时尝试恢复原规格。
     *
     * @param[in] format 模型支持的目标尺寸和帧率。
     */
    void update_camera_format(
        xmax_video_format format
    );

    /**
     * 读取当前摄像头规格，没有摄像头流时返回全零。
     */
    xmax_video_format camera_format();

    /**
     * 在生成中提交指定方向的档位，不锁定后续自动调档。
     *
     * @param[in] direction 0 为上行，1 为下行。
     * @param[in] level 目标档位，范围为 [1, 5]。
     */
    void quality_level(
        int direction,
        int level
    );

    /**
     * 读取包含原因和错误的当前状态快照。
     */
    xmax_event_info state_info();

    /**
     * 非阻塞读取带状态原因和错误详情的事件。
     *
     * @param[out] info 接收最早的待处理事件。
     * @return 是否取得事件。
     */
    bool poll(
        xmax_event_info& info
    ) {
        return coordinator_.poll(info);
    }

    /**
     * 按业务原因断开连接，保留本地媒体。
     *
     * @param[in] reason 正常停止或方向变化原因。
     */
    void disconnect_reason(
        xmax_realtime_reason reason
    );

    /**
     * 复制视口轨迹并预约异步发送，连续提交仅保留最新采样。
     *
     * @param[in] frame 采样点和实际视频布局。
     */
    void submit_interaction(
        const xmax_interaction_frame& frame
    );

    /**
     * 获取当前任务尚未过期的归一化轨迹副本。
     */
    std::vector<xmax_point> trajectory() const {
        if (coordinator_.state() != XMAX_GENERATING)
            return {};
        return render_.trajectory.snapshot();
    }

    /**
     * 读取本实时控制器使用的模型能力。
     */
    xmax_model_info model_info() const {
        return RealtimeModelDefinition::info(RealtimeModelDefinition::parse(api_->config().model));
    }

    /**
     * 委托渲染层在原生窗口显示结果帧及当前交互效果。
     *
     * @param[in] frame 待显示的结果帧，调用期间保持有效。
     * @param[in] window 当前线程拥有的 HWND。
     * @param[in] content_mode 等比例适应或居中填满。
     */
    void render_window(
        const Frame& frame,
        void* window,
        int32_t content_mode
    ) const {
        render_.render_window(frame, window, content_mode, coordinator_.state() == XMAX_GENERATING);
    }

  private:

    /**
     * 保留共享 API 服务，使子控制器在客户端句柄释放后仍可使用。
     */
    std::shared_ptr<ApiService> api_;

    /**
     * 管理服务端会话创建、保活和关闭的服务。
     */
    XmaxRealtimeConnectionManager connection_;

    /**
     * 负责 RTC 发布、生成信令及画质策略的控制器。
     */
    StreamController stream_;

    /**
     * 负责原生媒体或外部输入绑定的控制器。
     */
    MediaController media_;

    /**
     * 负责本地预览与远端结果最新帧缓冲区的控制器。
     */
    RenderController render_;

    /**
     * 与当前连接一致的输入请求批次，供媒体线程检查。
     */
    std::atomic<uint64_t> input_epoch_{0};

    /**
     * 当前媒体绑定批次，用于拒收旧绑定回调。
     */
    std::atomic<uint64_t> media_epoch_{0};

    /**
     * 摄像头绑定时保存的麦克风连接策略。
     */
    bool camera_microphone_ = false;

    /**
     * 生成条件及任务轨迹的统一管理器。
     */
    XmaxRealtimeGenerationManager generation_;

    /**
     * 生成管理器使用的条件类型。
     */
    using Context = XmaxRealtimeGenerationManager::Context;

    /**
     * 当前生命周期状态的原因和原始错误管理组件。
     */
    RealtimeErrorHandler errors_;

    /**
     * 最近一次活动会话标识，在断开后的状态快照中保留。
     */
    std::string last_session_id_;

    /**
     * 最近一次生成任务标识，在断开后的状态快照中保留。
     */
    std::string last_task_id_;

    /**
     * 连接与生成启动的统一阶段计时组件。
     */
    RealtimeTiming timing_;

    /**
     * 活动会话的心跳间隔，使用毫秒时长表示。
     */
    std::chrono::milliseconds heartbeat_interval_;

    /**
     * 执行生命周期操作、管理取消和投递应用事件的协调器。
     */
    RealtimeCoordinator coordinator_;

    /**
     * 保护可供界面线程读取的交互任务身份。
     */
    std::mutex interaction_mutex_;

    /**
     * 当前允许交互的任务标识，非生成状态为空。
     */
    std::string interaction_task_;

    /**
     * 公平调度单个轨迹采样，避免连续鼠标输入占满生命周期执行器。
     */
    void drain_interaction();

    /**
     * 更新会话状态，并向接入方投递对应状态事件。
     *
     * @param[in] state 新的实时生命周期状态。
     */
    void change_state(
        xmax_state state
    );

    /**
     * 清理 RTC 与服务端会话，并根据选项释放媒体和生成条件。
     *
     * @param[in] close 是否同时释放本地媒体和缓存生成条件。
     * @return 服务端会话已成功关闭时返回 true；关闭请求失败时返回 false 并投递错误事件。
     */
    bool cleanup(
        bool close
    );

    /**
     * 为活动连接执行到期的服务端与房间保活。
     */
    void heartbeat();

    /**
     * 在生成等待期间检查取消，并维持服务端和房间活跃。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void keepalive(
        const Cancel& cancel
    );

    /**
     * 清理失败操作持有的资源，并投递原始 SDK 错误。
     *
     * @param[in] error 需要保留错误码与诊断的原始 SDK 异常。
     */
    void fail(
        Error error
    );

    /**
     * 创建服务端会话并加入 RTC；取消或失败时回滚已分配资源。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] epoch 调用开始时捕获的取消批次，用于识别失效操作。
     * @param[in] microphone 是否在加入 RTC 房间时启动麦克风采集。
     */
    void connect_impl(
        const Cancel& cancel,
        uint64_t epoch,
        bool microphone
    );

    /**
     * 发送生成条件并接通输入帧交付，等待结果流首帧。
     *
     * @param[in] context 本次生成使用的提示词、参考图片和人脸修复条件。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void generate_impl(
        const Context& context,
        const Cancel& cancel
    );

    /**
     * 将本地预览绑定到新的媒体批次，拒收旧绑定的帧。
     */
    void bind_local();

    /**
     * 创建仅处理当前媒体绑定错误的回调，忽略旧绑定的回调。
     *
     * @param[in] token 当前连接或媒体绑定的批次，用于拒收旧回调。
     * @return 当前媒体绑定的错误回调；旧绑定触发的错误会被忽略。
     */
    std::function<void(Error)> media_failure(
        uint64_t token
    );
};
} // namespace xmax::detail
