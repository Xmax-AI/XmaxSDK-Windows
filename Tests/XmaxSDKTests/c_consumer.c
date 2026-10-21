#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma pack(push, 1)
#include <xmax/xmax.h>
struct caller_packed {
    char tag;
    uint64_t value;
};
#pragma pack(pop)
typedef char packing_restored[offsetof(struct caller_packed, value) == 1 ? 1 : -1];
#if UINTPTR_MAX == UINT64_MAX
typedef char original_client_layout[sizeof(xmax_client_config) == 32 ? 1 : -1];
typedef char original_frame_layout[sizeof(xmax_video_frame) == 96 ? 1 : -1];
typedef char original_context_layout[sizeof(xmax_generation_context) == 32 ? 1 : -1];
typedef char original_event_layout[sizeof(xmax_event) == 856 ? 1 : -1];
#endif
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            fprintf(stderr, "C ABI failure at line %d: %s\n", __LINE__, #expr);                              \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
int main(void) {
    uint32_t log_options = 99;
    xmax_log_language log_language = XMAX_LOG_ENGLISH;
    CHECK(xmax_logger_get_config(&log_options, &log_language) == XMAX_OK && log_options == XMAX_LOG_NONE);
    CHECK(sizeof(xmax_log_record) == 4160);
    CHECK(xmax_logger_configure(XMAX_LOG_ALL, XMAX_LOG_ENGLISH) == XMAX_OK);
    CHECK(xmax_logger_configure(8, XMAX_LOG_ENGLISH) == XMAX_INVALID_ARGUMENT);
    CHECK(xmax_logger_get_config(NULL, &log_language) == XMAX_INVALID_ARGUMENT);
    xmax_client client = NULL;
    xmax_realtime realtime = NULL;
    xmax_client_config c = {0};
    c.struct_size = sizeof(c);
    c.abi_version = XMAX_ABI_VERSION;
    c.api_key = "";
    CHECK(xmax_client_create(&c, &client) == XMAX_OK);
    struct {
        xmax_capabilities caps;
        uint64_t canary;
    } extended = {{0}, UINT64_C(0x123456789abcdef0)};
    extended.caps.struct_size = sizeof(extended);
    CHECK(xmax_client_get_capabilities(client, &extended.caps) == XMAX_OK);
    CHECK(extended.caps.abi_version == XMAX_ABI_VERSION && (extended.caps.flags & XMAX_CAP_STORAGE));
    CHECK(!(extended.caps.flags & XMAX_CAP_FRAME_INTERPOLATION));
    CHECK(extended.canary == UINT64_C(0x123456789abcdef0));
    xmax_storage storage = NULL;
    CHECK(xmax_storage_create(client, &storage) == XMAX_OK);
    CHECK(xmax_realtime_create(client, &realtime) == XMAX_OK);
    xmax_client_destroy(client); /* Children own their configuration and transport. */
    xmax_video_format f = {1920, 1024, 30};
    CHECK(xmax_realtime_prepare_external(realtime, &f) == XMAX_OK);
    xmax_log_record log_record = {0};
    CHECK(xmax_logger_poll(&log_record) == XMAX_INVALID_ARGUMENT);
    log_record.struct_size = sizeof(log_record);
    CHECK(xmax_logger_poll(&log_record) == XMAX_OK);
    CHECK(log_record.timestamp_us > 0 && log_record.message[0] && log_record.category[0]);
    CHECK(xmax_logger_configure(XMAX_LOG_NONE, XMAX_LOG_ENGLISH) == XMAX_OK);
    CHECK(xmax_logger_poll(&log_record) == XMAX_NO_EVENT);
    xmax_event e = {0};
    e.struct_size = sizeof(e);
    CHECK(xmax_realtime_get_state(realtime, &e) == XMAX_OK && e.state == XMAX_READY);
    CHECK(xmax_realtime_poll_event(realtime, &e) == XMAX_OK && e.state == XMAX_PREPARING);
    CHECK(xmax_realtime_poll_event(realtime, &e) == XMAX_OK && e.state == XMAX_READY);
    CHECK(xmax_realtime_poll_event(realtime, &e) == XMAX_NO_EVENT);
    xmax_video_format actual = {0};
    CHECK(xmax_realtime_get_format(realtime, &actual) == XMAX_OK && actual.width == 1920);
    CHECK(xmax_realtime_set_remote_volume(realtime, 0.5f) == XMAX_OK);
    float volume = 0;
    CHECK(xmax_realtime_get_remote_volume(realtime, &volume) == XMAX_OK && volume == 0.5f);
    xmax_quality_status quality = {0};
    quality.struct_size = sizeof(quality);
    CHECK(xmax_realtime_set_adaptive_quality(realtime, 0) == XMAX_OK);
    CHECK(xmax_realtime_get_quality(realtime, &quality) == XMAX_OK && !quality.adaptive_enabled);
    CHECK(xmax_realtime_set_interpolation(realtime, 1) == XMAX_UNSUPPORTED);
    CHECK(xmax_realtime_set_interpolation(realtime, 0) == XMAX_OK);
    xmax_encoding_config encoding = {sizeof(encoding), -1, 50, XMAX_ENCODER_AUTO};
    CHECK(xmax_realtime_set_encoding(realtime, &encoding) == XMAX_OK);
    CHECK(xmax_realtime_generate(realtime, NULL) == XMAX_INVALID_ARGUMENT);
    CHECK(xmax_realtime_connect(realtime, 0) == XMAX_INVALID_API_KEY); /* Empty key; no network. */
    const size_t stride = 1920 * 4, size = stride * 1024;
    uint8_t* pixels = (uint8_t*)calloc(size, 1);
    CHECK(pixels != NULL);
    pixels[0] = 41;
    xmax_video_frame input = {0};
    input.struct_size = sizeof(input);
    input.pixel_format = XMAX_BGRA;
    input.width = 1920;
    input.height = 1024;
    input.timestamp_us = 1;
    input.planes[0] = pixels;
    input.strides[0] = stride;
    input.plane_sizes[0] = size;
    CHECK(xmax_realtime_push_video_frame(realtime, &input) == XMAX_OK);
    pixels[0] = 99;
    free(pixels);
    xmax_frame held = NULL, absent = NULL;
    CHECK(xmax_realtime_acquire_local_frame(realtime, &held) == XMAX_OK && held != NULL);
    CHECK(xmax_realtime_acquire_local_frame(realtime, &absent) == XMAX_NO_EVENT && absent == NULL);
    xmax_video_frame view = {0};
    view.struct_size = sizeof(view);
    CHECK(xmax_frame_get_view(held, &view) == XMAX_OK && view.planes[0][0] == 41);
    CHECK(xmax_realtime_set_remote_volume(realtime, 2.0f) == XMAX_INVALID_ARGUMENT);
    CHECK(strlen(xmax_last_error()) > 0);
    CHECK(xmax_realtime_close(realtime) == XMAX_OK);
    CHECK(xmax_realtime_close(realtime) == XMAX_OK);
    xmax_realtime_destroy(realtime);
    CHECK(xmax_frame_get_view(held, &view) == XMAX_OK && view.planes[0][0] == 41);
    xmax_frame_release(held);
    xmax_transfer_progress progress = {0};
    progress.struct_size = sizeof(progress);
    CHECK(xmax_storage_get_progress(storage, &progress) == XMAX_OK && progress.state == XMAX_TRANSFER_IDLE);
    xmax_uploaded_file uploaded = NULL;
    xmax_upload_options options = {sizeof(options), "bad.txt", NULL, 0};
    const uint8_t data = 1;
    CHECK(xmax_storage_upload_data(storage, &data, 1, &options, &uploaded) == XMAX_INVALID_ARGUMENT &&
          uploaded == NULL);
    CHECK(xmax_storage_get_progress(storage, &progress) == XMAX_OK && progress.state == XMAX_TRANSFER_FAILED);
    xmax_storage_cancel(storage);
    xmax_storage_destroy(storage);
    CHECK(xmax_client_create(NULL, &client) == XMAX_INVALID_ARGUMENT && client == NULL);
    c.struct_size = 1;
    CHECK(xmax_client_create(&c, &client) == XMAX_INVALID_ARGUMENT);
    xmax_realtime_destroy(NULL);
    xmax_frame_release(NULL);
    xmax_string_release(NULL);
    xmax_storage_destroy(NULL);
    xmax_storage_cancel(NULL);
    xmax_camera_list_release(NULL);
    xmax_uploaded_file_release(NULL);
    CHECK(xmax_camera_list_count(NULL) == 0);
    puts("C ABI consumer passed");
    return 0;
}
