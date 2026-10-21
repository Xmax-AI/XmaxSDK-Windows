//
//  client.hpp
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
#include "realtime.hpp"
#include "storage.hpp"
#include "string.hpp"
#include "camera_device.hpp"
#include "XmaxConfiguration.hpp"
#include "RealtimeConfiguration.hpp"
#include <string>
#include <vector>

namespace xmax {

/**
 * 管理 SDK 配置并创建独立实时会话或存储控制器；创建客户端不会建立云端会话。
 */
class client : public detail::owner<xmax_client, xmax_client_destroy> {

  public:

    /**
     * 复制配置并创建 SDK 客户端，不启动云端会话。
     *
     * @param[in] key 创建时复制的 API 凭证，空文本允许进行本地准备。
     * @param[in] model 实时生成模型标识，当前仅支持 x2.1。
     * @see xmax_client_create
     */
    explicit client(
        const std::string& key,
        const std::string& model = "x2.1"
    ) {
        const xmax_client_config config{sizeof(config), XMAX_ABI_VERSION, key.c_str(), model.c_str(), 15000};
        check(xmax_client_create(&config, &value_));
    }

    /**
     * 按独立的服务环境与日志配置创建客户端。
     *
     * @param[in] config 全局配置；字符串在创建时复制。
     */
    explicit client(
        const XmaxConfiguration& config
    ) {
        const std::string key(config.api_key);
        const xmax_client_options options{
            sizeof(options),       XMAX_ABI_VERSION,
            key.c_str(),           static_cast<xmax_environment>(config.environment),
            config.logger_options, config.http_timeout_ms
        };
        check(xmax_client_create_with_options(&options, &value_));
    }

    /**
     * 查询实时模型支持的媒体规格与默认值。
     *
     * @param[in] model 实时生成模型。
     */
    static xmax_model_info model_info(
        RealtimeModel model
    ) {
        xmax_model_info info{};
        info.struct_size = sizeof(info);
        check(xmax_model_get_info(model_identifier(model), &info));
        return info;
    }

    /**
     * 接管源对象的资源所有权，移动后源对象为空。
     *
     * @param[in,out] other 转移资源所有权的源对象；移动完成后源对象为空。
     */
    client(
        client&& other
    ) noexcept = default;

    /**
     * 释放当前客户端句柄并接管源对象，源对象在移动后为空。
     *
     * @param[in,out] other 转移资源所有权的源对象；移动完成后源对象为空。
     * @return 当前客户端对象的引用。
     */
    client& operator=(
        client&& other
    ) noexcept = default;

    /**
     * 创建保留客户端配置的独立实时控制器。
     *
     * @return 拥有独立句柄生命周期、可移动而不可复制的实时控制器。
     * @see xmax_realtime_create
     */
    realtime create_realtime() const {
        xmax_realtime value = nullptr;
        check(xmax_realtime_create(get(), &value));
        return realtime(value);
    }

    /**
     * 按独立的实时模型配置创建控制器，沿用客户端的服务环境。
     *
     * @param[in] config 本次实时业务的配置。
     * @return 独立拥有媒体与连接生命周期的实时控制器。
     */
    realtime create_realtime(
        const RealtimeConfiguration& config
    ) const {
        const xmax_realtime_config options{sizeof(options), model_identifier(config.model)};
        xmax_realtime value = nullptr;
        check(xmax_realtime_create_with_config(get(), &options, &value));
        return realtime(value);
    }

    /**
     * 创建保留客户端配置的独立存储控制器。
     *
     * @return 拥有独立句柄生命周期、可移动而不可复制的存储控制器。
     * @see xmax_storage_create
     */
    storage create_storage() const {
        xmax_storage value = nullptr;
        check(xmax_storage_create(get(), &value));
        return storage(value);
    }

    /**
     * 检查当前 SDK 构建是否提供原生 RTC 后端。
     *
     * @see xmax_client_has_rtc
     */
    bool has_rtc() const {
        int32_t result = 0;
        check(xmax_client_has_rtc(get(), &result));
        return result != 0;
    }

    /**
     * 读取当前构建支持的功能与 ABI 版本。
     *
     * @return ABI 版本及当前构建支持的能力位。
     * @see xmax_client_get_capabilities
     */
    xmax_capabilities capabilities() const {
        xmax_capabilities value{};
        value.struct_size = sizeof(value);
        check(xmax_client_get_capabilities(get(), &value));
        return value;
    }

    /**
     * 枚举摄像头并将设备信息复制到调用方内存，不启动采集或云端会话。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     * @see xmax_client_list_cameras
     */
    std::vector<camera_device> cameras() const {
        xmax_camera_list value = nullptr;
        check(xmax_client_list_cameras(get(), &value));

        detail::owner<xmax_camera_list, xmax_camera_list_release> owned(value);
        std::vector<camera_device> result;

        for (size_t i = 0; i < xmax_camera_list_count(value); ++i) {
            xmax_camera_info info{};
            info.struct_size = sizeof(info);
            check(xmax_camera_list_get(value, i, &info));
            result.push_back({info.id, info.name, info.is_default != 0});
        }

        return result;
    }

    /**
     * 在已配置的 API 根地址下执行同步鉴权请求。
     *
     * @param[in] method HTTP 请求方法。
     * @param[in] path 相对当前模型 API 根地址的资源路径。
     * @param[in] json 可选 UTF-8 JSON 请求体，空指针表示不发送请求体。
     * @return 拥有响应 data JSON 文本的字符串对象。
     * @see xmax_client_request_json
     */
    string request(
        const std::string& method,
        const std::string& path,
        const char* json = nullptr
    ) const {
        xmax_string value = nullptr;
        check(xmax_client_request_json(get(), method.c_str(), path.c_str(), json, &value));
        return string(value);
    }
};
} // namespace xmax
