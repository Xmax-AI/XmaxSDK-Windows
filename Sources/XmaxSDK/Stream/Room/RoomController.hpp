//
//  RoomController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Stream/Room/RoomEvent.hpp"
#include "Stream/Room/RoomMessageSender.hpp"

namespace xmax::detail {

/**
 * 管理 RTC 房间的分配、加入、离开、跨线程发布和信令发送。
 */
class RoomController {

  public:

    /**
     * 保留房间适配器工厂，加入前不启动网络连接。
     *
     * @param[in] factory 创建原生 RTC 后端的工厂。
     */
    explicit RoomController(
        RtcFactory factory = make_rtc
    )
        : factory_(std::move(factory)) {
    }

    /**
     * 排空房间回调并释放原生后端。
     */
    ~RoomController() {
        leave();
    }

    /**
     * 在创建服务端会话前校验并分配原生 RTC 后端。
     */
    void allocate() {
        require(!rtc_, XMAX_INVALID_STATE, "An RTC backend is already allocated");
        rtc_ = factory_();
        require(bool(rtc_), XMAX_UNSUPPORTED, "No RTC backend is available");
    }

    /**
     * 加入服务端指定房间，成功后才允许媒体线程发布帧。
     *
     * @param[in] value 服务端分配的房间身份和凭证。
     * @param[in] encoding 本次上行视频编码配置。
     * @param[in] microphone 是否启动麦克风。
     * @param[in] callbacks 房间结果帧、统计和错误回调。
     * @param[in] cancel 协作式取消判断。
     */
    void join(
        const Connection& value,
        VideoEncodingConfiguration encoding,
        bool microphone,
        RtcCallbacks callbacks,
        const Cancel& cancel
    ) {
        require(rtc_ && !joined_, XMAX_INVALID_STATE, "Allocate an RTC backend before joining");
        connection = value;
        try {
            rtc_->join(value, encoding, microphone, std::move(callbacks), cancel);
            require(!cancel(), XMAX_CANCELLED, "RTC join cancelled");
            joined_ = true;
            std::atomic_store(&media_rtc_, rtc_);
        } catch (...) {
            leave();
            throw;
        }
    }

    /**
     * 关闭媒体线程入口，离开房间并排空回调；可重复调用。
     */
    void leave() noexcept {
        std::atomic_store(&media_rtc_, std::shared_ptr<Rtc>{});
        if (rtc_) {
            rtc_->leave();
            rtc_.reset();
        }
        joined_ = false;
        connection = {};
        task.clear();
        sender_.reset();
    }

    /**
     * 查询当前是否已完成房间加入。
     */
    bool joined() const {
        return joined_;
    }

    /**
     * 借用原生后端，仅供生命周期线程使用；离开房间后失效。
     */
    Rtc* backend() const {
        return rtc_.get();
    }

    /**
     * 通过原子房间快照发布视频帧，允许从媒体线程调用。
     *
     * @param[in] frame 调用期间保持有效的视频帧。
     */
    void push(
        const Frame& frame
    ) {
        const auto rtc = std::atomic_load(&media_rtc_);
        if (rtc)
            rtc->push(frame);
    }

    /**
     * 当前房间的连接信息，仅由生命周期线程访问。
     */
    Connection connection;

    /**
     * 当前生成任务标识，未生成时为空。
     */
    std::string task;

    /**
     * 通过同一个发送预算发送房间信令。
     *
     * @param[in] message 未分片的业务消息。
     * @param[in] cancel 协作式取消判断。
     * @param[in] maintain 等待预算期间的保活操作。
     */
    void send(
        const json& message,
        const Cancel& cancel,
        const std::function<void()>& maintain = {}
    ) {
        require(joined_, XMAX_INVALID_STATE, "No RTC room is connected");
        sender_.send(*rtc_, message, cancel, maintain);
    }

    /**
     * 为当前任务发送归一化轨迹，拒绝空点集和无效坐标。
     *
     * @param[in] points 按采样顺序排列、范围为 [0, 1] 的坐标。
     * @param[in] cancel 协作式取消判断。
     * @param[in] maintain 发送等待期间执行的保活回调。
     */
    void tracks(
        const std::vector<xmax_point>& points,
        const Cancel& cancel,
        const std::function<void()>& maintain
    ) {
        require(!task.empty(), XMAX_INVALID_STATE, "No generation task is active");
        json values = json::array();
        for (const auto& point : points)
            values.push_back(json::array({point.x, point.y}));
        send(
            {{"event", "tracks"}, {"tracks", std::move(values)}, {"user_id", connection.user}, {"uid", task}},
            cancel, maintain
        );
    }

  private:

    /**
     * 原生 RTC 后端工厂，成功分配后由当前房间管理。
     */
    RtcFactory factory_;

    /**
     * 生命周期线程拥有的原生 RTC 后端。
     */
    std::shared_ptr<Rtc> rtc_;

    /**
     * 媒体线程可原子读取的已加入房间后端。
     */
    std::shared_ptr<Rtc> media_rtc_;

    /**
     * 是否已经完成 RTC 加入，仅由生命周期线程访问。
     */
    bool joined_ = false;

    /**
     * 负责信令分片和滑动窗口预算的发送器。
     */
    RoomMessageSender sender_;
};
} // namespace xmax::detail
