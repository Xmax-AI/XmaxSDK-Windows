//
//  XmaxClient.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Core/Storage/XmaxStorageManager.hpp"
#include "Foundation/Render/WindowRenderer.hpp"

namespace xmax::detail {

/**
 * 提供共享 API 配置以及独立的实时会话和存储控制器工厂。
 */
class XmaxClient {

  public:

    /**
     * 复制 API 配置并保留共享 HTTP 传输器，供子控制器使用。
     *
     * @param[in] config 复制后保留的 API 凭证、模型标识和 HTTP 超时配置。
     * @param[in] http 共享的底层 HTTP 传输器，其生命周期由服务保留。
     */
    explicit XmaxClient(
        Config config,
        std::shared_ptr<Http> http = make_http()
    )
        : api_(std::make_shared<ApiService>(std::move(config), std::move(http))) {
        XmaxLogger::write(LogCategory::Realtime, XMAX_LOG_INFO, [&](auto language) {
            return XmaxLogger::localized(language, "创建 SDK Client (Client Created)", "Client Created") +
                   "\n└─ model=" + api_->config().model;
        });
    }

    /**
     * 执行经过鉴权的 API 请求，支持协作式取消。
     *
     * @param[in] method HTTP 请求方法。
     * @param[in] path 相对当前模型 API 根地址的资源路径。
     * @param[in] body 可选 JSON 请求体，空指针表示不发送请求体。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     * @return 服务响应中已通过状态校验的 data 负载。
     */
    json request(
        const std::string& method,
        const std::string& path,
        const json* body,
        const Cancel& cancel
    ) const {
        return api_->request(method, path, body, cancel);
    }

    /**
     * 创建具有独立生命周期和画质历史的实时控制器。
     *
     * @param[in] model 实时模型标识，空文本沿用客户端配置的模型。
     * @return 独占拥有的实时控制器，客户端对象销毁后仍可使用其配置。
     */
    std::unique_ptr<XmaxRealtimeManager> create_realtime(
        const std::string& model = ""
    ) const {
        auto api = api_;
        if (!model.empty() && model != api_->config().model) {
            auto config = api_->config();
            config.model = model;
            api = std::make_shared<ApiService>(std::move(config), api_->transport());
        }
        auto history = std::make_shared<QualityHistory>(api->config().model, make_preference_store());
        return std::make_unique<XmaxRealtimeManager>(
            api, make_rtc, std::chrono::seconds(10), make_camera_capture, std::move(history)
        );
    }

    /**
     * 检查当前 SDK 构建是否提供原生 RTC 后端。
     */
    bool has_rtc() const {
        return rtc_available();
    }

    /**
     * 读取当前构建提供的 C ABI 能力位。
     *
     * @return 由 XMAX_CAP_* 常量组成的能力位掩码。
     */
    uint64_t capabilities() const {
        return (rtc_available() ? XMAX_CAP_RTC : 0) | (camera_available() ? XMAX_CAP_CAMERA : 0) |
               (native_window_available() ? XMAX_CAP_NATIVE_WINDOW : 0) | XMAX_CAP_STORAGE | XMAX_CAP_TRAJECTORY;
    }

    /**
     * 枚举平台摄像头设备，不启动采集。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     */
    std::vector<CameraDevice> cameras() const {
        return make_camera_capture()->devices();
    }

    /**
     * 创建保留共享 API 服务的独立存储控制器。
     *
     * @return 独占拥有的存储控制器，客户端对象销毁后仍可使用其配置。
     */
    std::unique_ptr<XmaxStorageManager> create_storage() const {
        return std::make_unique<XmaxStorageManager>(api_);
    }

  private:

    /**
     * 保留共享 API 服务，使子控制器在客户端句柄释放后仍可使用。
     */
    std::shared_ptr<ApiService> api_;
};
} // namespace xmax::detail
