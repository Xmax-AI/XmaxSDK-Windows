#include "Core/XmaxClient.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
using namespace xmax::detail;
struct xmax_client_impl {
    XmaxClient client;
    explicit xmax_client_impl(Config config) : client(std::move(config)) {}
};
struct xmax_realtime_impl {
    std::unique_ptr<XmaxRealtimeManager> session;
    explicit xmax_realtime_impl(std::unique_ptr<XmaxRealtimeManager> value) : session(std::move(value)) {}
};
struct xmax_camera_list_impl {
    std::vector<CameraDevice> devices;
};
struct xmax_storage_impl {
    std::unique_ptr<XmaxStorageManager> storage;
};
struct xmax_uploaded_file_impl {
    StoredFile file;
};
struct xmax_frame_impl {
    std::shared_ptr<Frame> frame;
};
struct xmax_string_impl {
    std::string value;
};
namespace {
thread_local char last_error[1024]{};
void set_error(const char* message) noexcept {
    const auto length = std::min(std::strlen(message), sizeof(last_error) - 1);
    std::memcpy(last_error, message, length);
    last_error[length] = 0;
}
template <class F> xmax_result protect(F&& f) noexcept {
    try {
        last_error[0] = 0;
        f();
        return XMAX_OK;
    } catch (const Error& e) {
        set_error(e.what());
        return e.code;
    } catch (const json::exception&) {
        set_error("Invalid JSON or UTF-8 data");
        return XMAX_INVALID_ARGUMENT;
    } catch (const std::exception&) {
        set_error("Internal SDK operation failed");
        return XMAX_INTERNAL_ERROR;
    } catch (...) {
        set_error("Unknown SDK failure");
        return XMAX_INTERNAL_ERROR;
    }
}
template <class T> void sized(const T* value) {
    require(value && value->struct_size >= sizeof(T), XMAX_INVALID_ARGUMENT,
            "Missing or incompatible structure size");
}
void handle(const void* p) {
    require(p != nullptr, XMAX_INVALID_ARGUMENT, "Handle or output pointer is null");
}
std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return std::isspace(c); };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), ws));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), ws).base(), s.end());
    return s;
}
} // namespace
extern "C" {
const char* XMAX_CALL xmax_version(void) {
    return XMAX_VERSION;
}
const char* XMAX_CALL xmax_last_error(void) {
    return last_error;
}
xmax_result XMAX_CALL xmax_client_create(const xmax_client_config* c, xmax_client* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        sized(c);
        handle(out);
        require(c->abi_version == XMAX_ABI_VERSION, XMAX_INVALID_ARGUMENT, "Unsupported C ABI version");
        Config config;
        config.key = trim(c->api_key ? c->api_key : "");
        config.model = c->model ? c->model : "x2.1.1-preview-cn";
        config.timeout_ms = c->http_timeout_ms ? c->http_timeout_ms : 15000;
        auto client = std::make_unique<xmax_client_impl>(std::move(config));
        *out = client.release();
    });
}
void XMAX_CALL xmax_client_destroy(xmax_client client) {
    delete client;
}
xmax_result XMAX_CALL xmax_client_has_rtc(xmax_client client, int32_t* out) {
    return protect([&] {
        handle(client);
        handle(out);
        *out = client->client.has_rtc() ? 1 : 0;
    });
}
xmax_result XMAX_CALL xmax_client_get_capabilities(xmax_client client, xmax_capabilities* out) {
    return protect([&] {
        handle(client);
        sized(out);
        *out = {sizeof(*out), XMAX_ABI_VERSION, client->client.capabilities()};
    });
}
xmax_result XMAX_CALL xmax_client_list_cameras(xmax_client client, xmax_camera_list* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        handle(client);
        handle(out);
        *out = new xmax_camera_list_impl{client->client.cameras()};
    });
}
size_t XMAX_CALL xmax_camera_list_count(xmax_camera_list list) {
    return list ? list->devices.size() : 0;
}
xmax_result XMAX_CALL xmax_camera_list_get(xmax_camera_list list, size_t index, xmax_camera_info* out) {
    return protect([&] {
        handle(list);
        sized(out);
        require(index < list->devices.size(), XMAX_INVALID_ARGUMENT, "Camera index is out of range");
        const auto& device = list->devices[index];
        *out = {sizeof(*out), device.id.c_str(), device.name.c_str(), device.is_default ? 1 : 0};
    });
}
void XMAX_CALL xmax_camera_list_release(xmax_camera_list list) {
    delete list;
}
xmax_result XMAX_CALL xmax_realtime_create(xmax_client client, xmax_realtime* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        handle(client);
        handle(out);
        *out = new xmax_realtime_impl(client->client.create_realtime());
    });
}
void XMAX_CALL xmax_realtime_destroy(xmax_realtime realtime) {
    delete realtime;
}
xmax_result XMAX_CALL xmax_realtime_prepare_external(xmax_realtime r, const xmax_video_format* f) {
    return protect([&] {
        handle(r);
        handle(f);
        r->session->prepare(*f);
    });
}
xmax_result XMAX_CALL xmax_realtime_prepare_camera(xmax_realtime r, const xmax_camera_config* config) {
    return protect([&] {
        handle(r);
        sized(config);
        auto format = config->format;
        if (!format.width && !format.height && !format.fps)
            format = {1920, 1024, 30};
        r->session->prepare_camera(config->device_id ? config->device_id : "", format,
                                   config->use_microphone != 0);
    });
}
xmax_result XMAX_CALL xmax_realtime_switch_camera(xmax_realtime r, const char* device) {
    return protect([&] {
        handle(r);
        handle(device);
        r->session->switch_camera(device);
    });
}
xmax_result XMAX_CALL xmax_realtime_stop_media(xmax_realtime r) {
    return protect([&] {
        handle(r);
        r->session->stop_media();
    });
}
xmax_result XMAX_CALL xmax_realtime_connect(xmax_realtime r, int32_t microphone) {
    return protect([&] {
        handle(r);
        r->session->connect(microphone != 0);
    });
}
xmax_result XMAX_CALL xmax_realtime_generate(xmax_realtime r, const xmax_generation_context* context) {
    return protect([&] {
        handle(r);
        if (context) {
            sized(context);
            handle(context->prompt);
        }
        r->session->generate(context);
    });
}
xmax_result XMAX_CALL xmax_realtime_get_state(xmax_realtime r, xmax_event* out) {
    return protect([&] {
        handle(r);
        sized(out);
        *out = r->session->state_snapshot();
    });
}
xmax_result XMAX_CALL xmax_realtime_get_format(xmax_realtime r, xmax_video_format* out) {
    return protect([&] {
        handle(r);
        handle(out);
        *out = r->session->format();
    });
}
xmax_result XMAX_CALL xmax_realtime_get_remote_volume(xmax_realtime r, float* out) {
    return protect([&] {
        handle(r);
        handle(out);
        *out = r->session->volume();
    });
}
xmax_result XMAX_CALL xmax_realtime_set_encoding(xmax_realtime r, const xmax_encoding_config* config) {
    return protect([&] {
        handle(r);
        sized(config);
        r->session->encoding(*config);
    });
}
xmax_result XMAX_CALL xmax_realtime_set_adaptive_quality(xmax_realtime r, int32_t enabled) {
    return protect([&] {
        handle(r);
        r->session->adaptive(enabled != 0);
    });
}
xmax_result XMAX_CALL xmax_realtime_get_quality(xmax_realtime r, xmax_quality_status* out) {
    return protect([&] {
        handle(r);
        sized(out);
        *out = r->session->quality();
    });
}
xmax_result XMAX_CALL xmax_realtime_set_interpolation(xmax_realtime r, int32_t enabled) {
    return protect([&] {
        handle(r);
        r->session->interpolation(enabled != 0);
    });
}
xmax_result XMAX_CALL xmax_realtime_start(xmax_realtime r, const xmax_generation_context* c,
                                          int32_t microphone) {
    return protect([&] {
        handle(r);
        sized(c);
        handle(c->prompt);
        r->session->start(c->prompt, c->reference_path ? c->reference_path : "", c->face_restoration != 0,
                          microphone != 0);
    });
}
xmax_result XMAX_CALL xmax_realtime_update(xmax_realtime r, const xmax_generation_context* c) {
    return protect([&] {
        handle(r);
        sized(c);
        handle(c->prompt);
        r->session->update(c->prompt, c->reference_path ? c->reference_path : "", c->face_restoration != 0);
    });
}
xmax_result XMAX_CALL xmax_realtime_disconnect(xmax_realtime r) {
    return protect([&] {
        handle(r);
        r->session->disconnect(false);
    });
}
xmax_result XMAX_CALL xmax_realtime_close(xmax_realtime r) {
    return protect([&] {
        handle(r);
        r->session->disconnect(true);
    });
}
xmax_result XMAX_CALL xmax_realtime_is_requesting_frames(xmax_realtime r, int32_t* out) {
    return protect([&] {
        handle(r);
        handle(out);
        *out = r->session->requesting() ? 1 : 0;
    });
}
xmax_result XMAX_CALL xmax_realtime_push_video_frame(xmax_realtime r, const xmax_video_frame* f) {
    return protect([&] {
        handle(r);
        sized(f);
        r->session->push(*f);
    });
}
xmax_result XMAX_CALL xmax_realtime_set_remote_volume(xmax_realtime r, float v) {
    return protect([&] {
        handle(r);
        r->session->volume(v);
    });
}
xmax_result XMAX_CALL xmax_realtime_poll_event(xmax_realtime r, xmax_event* out) {
    bool found = false;
    auto result = protect([&] {
        handle(r);
        sized(out);
        found = r->session->poll(*out);
    });
    return result == XMAX_OK && !found ? XMAX_NO_EVENT : result;
}
xmax_result XMAX_CALL xmax_realtime_acquire_frame(xmax_realtime r, xmax_frame* out) {
    if (out)
        *out = nullptr;
    bool found = false;
    auto result = protect([&] {
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
xmax_result XMAX_CALL xmax_realtime_acquire_local_frame(xmax_realtime r, xmax_frame* out) {
    if (out)
        *out = nullptr;
    bool found = false;
    const auto result = protect([&] {
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
xmax_result XMAX_CALL xmax_frame_render_window(xmax_frame frame, void* hwnd, int32_t mode) {
    return protect([&] {
        handle(frame);
        render_window(*frame->frame, hwnd, mode);
    });
}
xmax_result XMAX_CALL xmax_storage_create(xmax_client client, xmax_storage* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        handle(client);
        handle(out);
        *out = new xmax_storage_impl{client->client.create_storage()};
    });
}
void XMAX_CALL xmax_storage_destroy(xmax_storage value) {
    delete value;
}
void XMAX_CALL xmax_storage_cancel(xmax_storage value) {
    if (value)
        value->storage->cancel();
}
xmax_result XMAX_CALL xmax_storage_get_progress(xmax_storage value, xmax_transfer_progress* out) {
    return protect([&] {
        handle(value);
        sized(out);
        *out = value->storage->progress();
    });
}
xmax_result XMAX_CALL xmax_storage_upload_file(xmax_storage value, const char* path,
                                               const xmax_upload_options* options, xmax_uploaded_file* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
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
xmax_result XMAX_CALL xmax_storage_upload_data(xmax_storage value, const uint8_t* data, size_t length,
                                               const xmax_upload_options* options, xmax_uploaded_file* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
        handle(value);
        handle(data);
        sized(options);
        handle(options->file_name);
        handle(out);
        auto file = value->storage->upload_data(data, length, options->file_name,
                                                options->content_type ? options->content_type : "",
                                                options->check_image_safety != 0);
        *out = new xmax_uploaded_file_impl{std::move(file)};
    });
}
xmax_result XMAX_CALL xmax_storage_download(xmax_storage value, const char* url, const char* destination,
                                            uint64_t* bytes) {
    if (bytes)
        *bytes = 0;
    return protect([&] {
        handle(value);
        handle(url);
        handle(destination);
        handle(bytes);
        *bytes = value->storage->download(url, destination);
    });
}
xmax_result XMAX_CALL xmax_uploaded_file_get_info(xmax_uploaded_file file, xmax_uploaded_file_info* out) {
    return protect([&] {
        handle(file);
        sized(out);
        *out = {sizeof(*out), file->file.url.c_str(), file->file.object_key.c_str(),
                file->file.etag.empty() ? nullptr : file->file.etag.c_str()};
    });
}
void XMAX_CALL xmax_uploaded_file_release(xmax_uploaded_file file) {
    delete file;
}
xmax_result XMAX_CALL xmax_frame_get_view(xmax_frame frame, xmax_video_frame* view) {
    return protect([&] {
        handle(frame);
        sized(view);
        *view = frame->frame->view();
    });
}
void XMAX_CALL xmax_frame_release(xmax_frame frame) {
    delete frame;
}
xmax_result XMAX_CALL xmax_client_request_json(xmax_client c, const char* method, const char* path,
                                               const char* body, xmax_string* out) {
    if (out)
        *out = nullptr;
    return protect([&] {
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
const char* XMAX_CALL xmax_string_data(xmax_string value) {
    return value ? value->value.c_str() : nullptr;
}
void XMAX_CALL xmax_string_release(xmax_string value) {
    delete value;
}
}
