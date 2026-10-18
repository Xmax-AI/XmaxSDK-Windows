#pragma once
#include "xmax.h"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace xmax {
class error : public std::runtime_error {
  public:
    error(xmax_result code, const char* message) : std::runtime_error(message), code_(code) {}
    xmax_result code() const noexcept { return code_; }

  private:
    xmax_result code_;
};
inline void check(xmax_result result) {
    if (result < 0)
        throw error(result, xmax_last_error());
}
namespace detail {
template <class T, void(XMAX_CALL* Release)(T)> class owner {
  public:
    explicit owner(T value = nullptr) noexcept : value_(value) {}
    ~owner() { Release(value_); }
    owner(const owner&) = delete;
    owner& operator=(const owner&) = delete;
    owner(owner&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    owner& operator=(owner&& other) noexcept {
        if (this != &other) {
            Release(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    T get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }

  protected:
    T value_;
};
} // namespace detail
class string : public detail::owner<xmax_string, xmax_string_release> {
  public:
    using owner::owner;
    std::string value() const {
        const auto* text = xmax_string_data(get());
        return text ? text : "";
    }
};
class frame : public detail::owner<xmax_frame, xmax_frame_release> {
  public:
    using owner::owner;
    xmax_video_frame view() const {
        xmax_video_frame result{};
        result.struct_size = sizeof(result);
        check(xmax_frame_get_view(get(), &result));
        return result;
    }
    void render_window(void* hwnd, int32_t mode = XMAX_CONTENT_FIT) const {
        check(xmax_frame_render_window(get(), hwnd, mode));
    }
};
struct camera_device {
    std::string id, name;
    bool is_default;
};
struct uploaded_file_info {
    std::string url, object_key, etag;
};
class uploaded_file : public detail::owner<xmax_uploaded_file, xmax_uploaded_file_release> {
  public:
    using owner::owner;
    uploaded_file_info info() const {
        xmax_uploaded_file_info result{};
        result.struct_size = sizeof(result);
        check(xmax_uploaded_file_get_info(get(), &result));
        return {result.url, result.object_key, result.etag ? result.etag : ""};
    }
};
class storage : public detail::owner<xmax_storage, xmax_storage_destroy> {
  public:
    using owner::owner;
    void cancel() const noexcept { xmax_storage_cancel(get()); }
    xmax_transfer_progress progress() const {
        xmax_transfer_progress value{};
        value.struct_size = sizeof(value);
        check(xmax_storage_get_progress(get(), &value));
        return value;
    }
    uploaded_file upload_file(const std::string& path, bool check_safety = false,
                              const std::string& content_type = {}) {
        const xmax_upload_options options{
            sizeof(options), nullptr, content_type.empty() ? nullptr : content_type.c_str(), check_safety};
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_file(get(), path.c_str(), &options, &result));
        return uploaded_file(result);
    }
    uploaded_file upload_data(const uint8_t* data, size_t size, const std::string& name,
                              const std::string& type = {}, bool check_safety = false) {
        const xmax_upload_options options{sizeof(options), name.c_str(),
                                          type.empty() ? nullptr : type.c_str(), check_safety};
        xmax_uploaded_file result = nullptr;
        check(xmax_storage_upload_data(get(), data, size, &options, &result));
        return uploaded_file(result);
    }
    uint64_t download(const std::string& url, const std::string& destination) {
        uint64_t bytes = 0;
        check(xmax_storage_download(get(), url.c_str(), destination.c_str(), &bytes));
        return bytes;
    }
};
class realtime : public detail::owner<xmax_realtime, xmax_realtime_destroy> {
  public:
    using owner::owner;
    void prepare(xmax_video_format format) { check(xmax_realtime_prepare_external(get(), &format)); }
    void prepare_camera(const std::string& device = {}, xmax_video_format format = {1920, 1024, 30},
                        bool microphone = false) {
        const xmax_camera_config config{sizeof(config), device.empty() ? nullptr : device.c_str(), format,
                                        microphone};
        check(xmax_realtime_prepare_camera(get(), &config));
    }
    void switch_camera(const std::string& device) {
        check(xmax_realtime_switch_camera(get(), device.c_str()));
    }
    void stop_media() { check(xmax_realtime_stop_media(get())); }
    void connect(bool microphone = false) { check(xmax_realtime_connect(get(), microphone)); }
    void start(const std::string& prompt, const std::string& reference = {}, bool microphone = false,
               bool face = false) {
        const xmax_generation_context context{sizeof(context), prompt.c_str(),
                                              reference.empty() ? nullptr : reference.c_str(), face};
        check(xmax_realtime_start(get(), &context, microphone));
    }
    void generate() { check(xmax_realtime_generate(get(), nullptr)); }
    void generate(const std::string& prompt, const std::string& reference = {}, bool face = false) {
        const xmax_generation_context context{sizeof(context), prompt.c_str(),
                                              reference.empty() ? nullptr : reference.c_str(), face};
        check(xmax_realtime_generate(get(), &context));
    }
    void update(const std::string& prompt, const std::string& reference = {}, bool face = false) {
        const xmax_generation_context context{sizeof(context), prompt.c_str(),
                                              reference.empty() ? nullptr : reference.c_str(), face};
        check(xmax_realtime_update(get(), &context));
    }
    bool requesting_frames() const {
        int32_t result = 0;
        check(xmax_realtime_is_requesting_frames(get(), &result));
        return result != 0;
    }
    void push(const xmax_video_frame& value) { check(xmax_realtime_push_video_frame(get(), &value)); }
    void volume(float value) { check(xmax_realtime_set_remote_volume(get(), value)); }
    float volume() const {
        float value = 0;
        check(xmax_realtime_get_remote_volume(get(), &value));
        return value;
    }
    xmax_event state() const {
        xmax_event value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_state(get(), &value));
        return value;
    }
    xmax_video_format format() const {
        xmax_video_format value{};
        check(xmax_realtime_get_format(get(), &value));
        return value;
    }
    bool poll(xmax_event& event) const {
        event.struct_size = sizeof(event);
        const auto result = xmax_realtime_poll_event(get(), &event);
        check(result);
        return result == XMAX_OK;
    }
    frame acquire_frame() const {
        xmax_frame value = nullptr;
        check(xmax_realtime_acquire_frame(get(), &value));
        return frame(value);
    }
    frame acquire_local_frame() const {
        xmax_frame value = nullptr;
        check(xmax_realtime_acquire_local_frame(get(), &value));
        return frame(value);
    }
    void encoding(int minimum = -1, int maximum = 0, int preference = XMAX_ENCODER_MAINTAIN_FRAMERATE) {
        const xmax_encoding_config value{sizeof(value), minimum, maximum, preference};
        check(xmax_realtime_set_encoding(get(), &value));
    }
    void adaptive_quality(bool enabled) { check(xmax_realtime_set_adaptive_quality(get(), enabled)); }
    xmax_quality_status quality() const {
        xmax_quality_status value{};
        value.struct_size = sizeof(value);
        check(xmax_realtime_get_quality(get(), &value));
        return value;
    }
    void interpolation(bool enabled) { check(xmax_realtime_set_interpolation(get(), enabled)); }
    void disconnect() { check(xmax_realtime_disconnect(get())); }
    void close() { check(xmax_realtime_close(get())); }
};
class client : public detail::owner<xmax_client, xmax_client_destroy> {
  public:
    explicit client(const std::string& key, const std::string& model = "x2.1.1-preview-cn") {
        const xmax_client_config config{sizeof(config), XMAX_ABI_VERSION, key.c_str(), model.c_str(), 15000};
        check(xmax_client_create(&config, &value_));
    }
    client(client&&) noexcept = default;
    client& operator=(client&&) noexcept = default;
    realtime create_realtime() const {
        xmax_realtime value = nullptr;
        check(xmax_realtime_create(get(), &value));
        return realtime(value);
    }
    storage create_storage() const {
        xmax_storage value = nullptr;
        check(xmax_storage_create(get(), &value));
        return storage(value);
    }
    bool has_rtc() const {
        int32_t result = 0;
        check(xmax_client_has_rtc(get(), &result));
        return result != 0;
    }
    xmax_capabilities capabilities() const {
        xmax_capabilities value{};
        value.struct_size = sizeof(value);
        check(xmax_client_get_capabilities(get(), &value));
        return value;
    }
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
    string request(const std::string& method, const std::string& path, const char* json = nullptr) const {
        xmax_string value = nullptr;
        check(xmax_client_request_json(get(), method.c_str(), path.c_str(), json, &value));
        return string(value);
    }
};
} // namespace xmax
