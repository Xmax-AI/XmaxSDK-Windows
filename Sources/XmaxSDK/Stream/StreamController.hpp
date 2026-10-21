//
//  StreamController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Quality/QualityController.hpp"
#include "Stream/Quality/QualityHistory.hpp"
#include "Stream/Room/RoomEvent.hpp"
#include "Stream/Room/RoomController.hpp"
#include "Stream/Encoding/EncodingController.hpp"

namespace xmax::detail {

/**
 * 管理 RTC 发布、生成信令和双向画质；生命周期操作需串行调用，帧推送允许来自媒体线程。
 */
class StreamController {

  public:

    /**
     * 保留原生 RTC 工厂与可选画质历史后端。
     */
    explicit StreamController(
        RtcFactory factory = make_rtc,
        std::shared_ptr<QualityHistory> history = {}
    )
        : room_(std::move(factory)), history_(std::move(history)) {
    }

    /**
     * 离开 RTC 房间并排空内部回调，释放发布与信令资源。
     */
    ~StreamController();

    /**
     * 在分配服务端会话前创建原生 RTC 适配器。
     */
    void allocate();

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
    void join(
        const Connection& connection,
        xmax_video_format format,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel,
        xmax_video_format generation_format = {}
    );

    /**
     * 创建生成任务并发送启动信令，等待期间执行必要的维护。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] maintain 等待或限速期间执行的维护回调，空函数表示无需额外维护。
     */
    void start(
        const std::string& prompt,
        const std::string& reference,
        bool face,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    );

    /**
     * 等待真实结果首帧，同时检查取消并执行保活。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] keepalive 等待期间周期执行的保活函数，不得持有跨线程生命周期锁。
     */
    void wait_remote(
        const Cancel& cancel,
        const std::function<void()>& keepalive
    );

    /**
     * 使用当前任务标识发送替换后的生成条件。
     *
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 参考图片路径或服务可接受的地址，空文本表示不提供参考图片。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] maintain 等待或限速期间执行的维护回调，空函数表示无需额外维护。
     */
    void update(
        const std::string& prompt,
        const std::string& reference,
        bool face,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    );

    /**
     * 为当前生成任务发送房间保活信令。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void heartbeat(
        const Cancel& cancel
    );

    /**
     * 解除媒体线程的 RTC 引用，离开房间并清除信令状态。
     */
    void stop() noexcept;

    /**
     * 通过活动 RTC 适配器的线程安全快照发布一帧视频。
     *
     * @param[in] frame 待交付的视频帧或借用视图；同步调用返回前像素必须有效。
     */
    void push(
        const Frame& frame
    );

    /**
     * 设置并保存当前或下次连接使用的远端播放音量。
     *
     * @param[in] value 范围为 [0, 1] 的有限音量值，0 表示静音。
     */
    void volume(
        float value
    );

    /**
     * 读取当前或下次连接使用的远端播放音量。
     *
     * @return 范围为 [0, 1] 的播放音量，初始值为 0。
     */
    float volume() const {
        return volume_;
    }

    /**
     * 停止当前生成任务，保留 RTC 房间连接。
     *
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void stop_generation(
        const Cancel& cancel
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
    ) {
        quality_.enabled = enabled;
        quality_.reset_samples();
    }

    /**
     * 读取当前双向画质档位及生效的视频规格。
     *
     * @return 当前自适应开关、上下行画质档位以及实际使用的视频尺寸和帧率。
     */
    xmax_quality_status quality() const;

    /**
     * 更新指定方向的网络质量策略，返回由此产生的质量或性能事件。
     *
     * @param[in] direction 传输方向，0 为上行，1 为下行。
     * @param[in] network_quality 当前方向的原生网络质量枚举值。
     * @param[in] sampled_at 网络样本产生时的单调秒时间，过期或重复样本不参与决策。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @return 本次观察触发的事件集合；无变更时为空。
     */
    std::vector<xmax_event>
    observe_quality(
        int direction,
        int network_quality,
        double sampled_at,
        const Cancel& cancel
    );

    /**
     * 应用接入方指定的单向档位，失败时保持旧档位。
     *
     * @param[in] direction 0 为上行，1 为下行。
     * @param[in] level 目标档位，范围为 [1, 5]。
     * @param[in] cancel 协作式取消判断。
     * @param[in] maintain 信令等待期间执行的保活回调。
     * @return 成功提交后的画质变更事件。
     */
    xmax_event
    set_quality(
        int direction,
        int level,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    );

    /**
     * 发送当前生成任务的归一化轨迹。
     *
     * @param[in] points 归一化点集。
     * @param[in] cancel 协作式取消判断。
     * @param[in] maintain 信令等待期间的保活回调。
     */
    void tracks(
        const std::vector<xmax_point>& points,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        require(room_.joined() && !room_.task.empty(), XMAX_INVALID_STATE, "No generation is active");
        room_.tracks(points, cancel, maintain);
    }

    /**
     * 借用活动生成任务的标识。
     *
     * @return 当前任务标识的借用引用，仅在控制器未销毁且任务未更改时有效。
     */
    const std::string& task() const {
        return room_.task;
    }

  private:

    /**
     * 负责房间生命周期、发布入口和受限速保护的信令通道。
     */
    RoomController room_;

    /**
     * 外部输入规格或摄像头编码目标，作为上下行画质档位的计算基准。
     */
    xmax_video_format format_{};

    /**
     * 服务端生成画布，由模型规则解析，独立于采集帧和编码目标。
     */
    xmax_video_format generation_format_{};

    /**
     * 当前或下一次连接使用的远端播放音量，范围为 [0, 1]。
     */
    float volume_ = 0;

    /**
     * 上行与下行画质策略及自适应开关。
     */
    QualityController quality_;

    /**
     * 共享的按模型和方向保存的有效画质历史。
     */
    std::shared_ptr<QualityHistory> history_;

    /**
     * 分别记录两个方向最近一次画质缓存写入的单调时间点。
     */
    Clock::time_point quality_saved_[2]{};

    /**
     * 接入方指定的上行码率限制和降级偏好。
     */
    EncodingController encoding_;

    /**
     * 将当前档位和码率覆盖配置解析为原生编码参数。
     *
     * @param[in] level 画质档位，范围为 [1, 5]。
     */
    VideoEncodingConfiguration uplink_encoding(
        int level
    ) const;

    /**
     * 使用活动房间、任务标识和生成条件构造房间信令。
     *
     * @param[in] event 房间信令的事件名称。
     * @param[in] prompt UTF-8 生成提示词。
     * @param[in] reference 可选的生成参考图片路径或地址。
     * @param[in] face 是否启用生成条件中的人脸修复。
     * @return 可供房间信令编码和发送的生成事件 JSON。
     */
    json generation_message(
        const std::string& event,
        const std::string& prompt,
        const std::string& reference,
        bool face
    ) const;
};
} // namespace xmax::detail
