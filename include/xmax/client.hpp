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
 * Manages SDK configuration and creates independent realtime or storage controllers without opening a cloud
 * session.
 */
class client : public detail::owner<xmax_client, xmax_client_destroy> {

  public:

    /**
     * Copies configuration and creates an SDK client without opening a cloud session.
     *
     * @param[in] key The API credential copied during creation; empty permits local preparation.
     * @param[in] model The realtime model identifier; only x2.1 is supported.
     *
     * @see xmax_client_create
     */
    explicit client(
        const std::string& key,
        const std::string& model = "x2.1"
    ) {
        const xmax_client_config config{
            sizeof(config),
            XMAX_ABI_VERSION,
            key.c_str(),
            model.c_str(),
            15000
        };
        check(xmax_client_create(
            &config,
            &value_
        ));
    }

    /**
     * Creates a client with independent service-environment and logging configuration.
     *
     * @param[in] config The global configuration; strings are copied during creation.
     */
    explicit client(
        const XmaxConfiguration& config
    ) {
        const std::string key(config.api_key);
        const xmax_client_options options{
            sizeof(options),
            XMAX_ABI_VERSION,
            key.c_str(),
            static_cast<xmax_environment>(config.environment),
            config.logger_options,
            config.http_timeout_ms
        };
        check(xmax_client_create_with_options(
            &options,
            &value_
        ));
    }

    /**
     * Returns supported media formats and defaults for a realtime model.
     *
     * @param[in] model The realtime generation model.
     */
    static xmax_model_info model_info(
        RealtimeModel model
    ) {
        xmax_model_info info{};
        info.struct_size = sizeof(info);
        check(xmax_model_get_info(
            model_identifier(model),
            &info
        ));
        return info;
    }

    /**
     * Transfers resource ownership and leaves the source empty.
     *
     * @param[in,out] other The ownership source, left empty after the move.
     */
    client(
        client&& other
    ) noexcept = default;

    /**
     * Releases this client handle and transfers ownership from the source, leaving it empty.
     *
     * @param[in,out] other The ownership source, left empty after the move.
     *
     * @return A reference to this client.
     */
    client& operator=(
        client&& other
    ) noexcept = default;

    /**
     * Creates an independent realtime controller retaining the client configuration.
     *
     * @return A move-only realtime controller with an independent handle lifetime.
     *
     * @see xmax_realtime_create
     */
    realtime create_realtime() const {
        xmax_realtime value = nullptr;
        check(xmax_realtime_create(
            get(),
            &value
        ));
        return realtime(value);
    }

    /**
     * Creates a controller with its own model configuration and the client's service environment.
     *
     * @param[in] config The realtime controller's configuration.
     *
     * @return A controller independently owning media and connection lifecycles.
     */
    realtime create_realtime(
        const RealtimeConfiguration& config
    ) const {
        const xmax_realtime_options options{
            sizeof(options),
            model_identifier(config.model),
            config.is_adaptive_quality_enabled
        };
        xmax_realtime value = nullptr;
        check(xmax_realtime_create_with_options(
            get(),
            &options,
            &value
        ));
        return realtime(value);
    }

    /**
     * Creates an independent storage controller retaining the client configuration.
     *
     * @return A move-only storage controller with an independent handle lifetime.
     *
     * @see xmax_storage_create
     */
    storage create_storage() const {
        xmax_storage value = nullptr;
        check(xmax_storage_create(
            get(),
            &value
        ));
        return storage(value);
    }

    /**
     * Reports whether this SDK build provides a native RTC backend.
     *
     * @see xmax_client_has_rtc
     */
    bool has_rtc() const {
        int32_t result = 0;
        check(xmax_client_has_rtc(
            get(),
            &result
        ));
        return result != 0;
    }

    /**
     * Returns the build's supported features and ABI version.
     *
     * @return The ABI version and capability flags supported by this build.
     *
     * @see xmax_client_get_capabilities
     */
    xmax_capabilities capabilities() const {
        xmax_capabilities value{};
        value.struct_size = sizeof(value);
        check(xmax_client_get_capabilities(
            get(),
            &value
        ));
        return value;
    }

    /**
     * Enumerates cameras and copies device information into app-owned storage without starting capture or a
     * cloud session.
     *
     * @return A camera information snapshot, or an empty collection if no cameras are available.
     *
     * @see xmax_client_list_cameras
     */
    std::vector<camera_device> cameras() const {
        xmax_camera_list value = nullptr;
        check(xmax_client_list_cameras(
            get(),
            &value
        ));

        detail::owner<xmax_camera_list, xmax_camera_list_release> owned(value);
        std::vector<camera_device> result;

        for (size_t i = 0; i < xmax_camera_list_count(value); ++i) {
            xmax_camera_info info{};
            info.struct_size = sizeof(info);
            check(xmax_camera_list_get(
                value,
                i,
                &info
            ));
            result.push_back({info.id, info.name, info.is_default != 0});
        }

        return result;
    }

    /**
     * Performs a synchronous authenticated request under the configured API root.
     *
     * @param[in] method The HTTP request method.
     * @param[in] path The resource path relative to the current model's API root.
     * @param[in] json An optional UTF-8 JSON body; a null pointer omits it.
     *
     * @return An owned string containing the response's data JSON.
     *
     * @see xmax_client_request_json
     */
    string request(
        const std::string& method,
        const std::string& path,
        const char* json = nullptr
    ) const {
        xmax_string value = nullptr;
        check(xmax_client_request_json(
            get(),
            method.c_str(),
            path.c_str(),
            json,
            &value
        ));
        return string(value);
    }
};
} // namespace xmax
