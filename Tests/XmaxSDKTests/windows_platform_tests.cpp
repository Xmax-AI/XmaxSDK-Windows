#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <xmax/xmax.h>
#define CHECK(value)                                                                                         \
    do {                                                                                                     \
        if (!(value))                                                                                        \
            throw std::runtime_error(#value);                                                                \
    } while (0)
int main() {
    xmax_client client = nullptr;
    xmax_realtime realtime = nullptr;
    xmax_camera_list cameras = nullptr;
    xmax_frame frame = nullptr;
    HWND window = nullptr;
    try {
        xmax_client_config config{sizeof(config), XMAX_ABI_VERSION, "", nullptr, 15000};
        CHECK(xmax_client_create(&config, &client) == XMAX_OK);
        CHECK(xmax_client_list_cameras(client, &cameras) == XMAX_OK);
        CHECK(xmax_realtime_create(client, &realtime) == XMAX_OK);
        xmax_client_destroy(client);
        client = nullptr;
        for (size_t i = 0; i < xmax_camera_list_count(cameras); ++i) {
            xmax_camera_info info{};
            info.struct_size = sizeof(info);
            CHECK(xmax_camera_list_get(cameras, i, &info) == XMAX_OK);
            CHECK(info.id && info.name && *info.id);
        }
        xmax_camera_list_release(cameras);
        cameras = nullptr;
        xmax_video_format format{1920, 1024, 30};
        CHECK(xmax_realtime_prepare_external(realtime, &format) == XMAX_OK);
        std::vector<uint8_t> pixels(size_t(format.width) * format.height * 4, 255);
        xmax_video_frame input{};
        input.struct_size = sizeof(input);
        input.pixel_format = XMAX_BGRA;
        input.width = format.width;
        input.height = format.height;
        input.timestamp_us = 1;
        input.planes[0] = pixels.data();
        input.strides[0] = size_t(format.width) * 4;
        input.plane_sizes[0] = pixels.size();
        CHECK(xmax_realtime_push_video_frame(realtime, &input) == XMAX_OK);
        CHECK(xmax_realtime_acquire_local_frame(realtime, &frame) == XMAX_OK);
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"XmaxSDK.NativeTest";
        CHECK(RegisterClassW(&type) != 0);
        window = CreateWindowExW(0, type.lpszClassName, L"SDK native rendering test", WS_OVERLAPPEDWINDOW, 0,
                                 0, 320, 240, nullptr, nullptr, type.hInstance, nullptr);
        CHECK(window);
        CHECK(xmax_frame_render_window(frame, window, XMAX_CONTENT_FIT) == XMAX_OK);
        CHECK(xmax_frame_render_window(frame, window, XMAX_CONTENT_FILL) == XMAX_OK);
        auto wrong_thread = std::async(std::launch::async, [&] {
            const auto result = xmax_frame_render_window(frame, window, XMAX_CONTENT_FIT);
            CHECK(std::string(xmax_last_error()).size() > 0);
            return result;
        });
        CHECK(wrong_thread.get() == XMAX_INVALID_STATE);
        CHECK(std::string(xmax_last_error()).empty()); // Last-error storage is thread local.
        CHECK(xmax_frame_render_window(frame, window, 99) == XMAX_INVALID_ARGUMENT);
        DestroyWindow(window);
        CHECK(xmax_frame_render_window(frame, window, XMAX_CONTENT_FIT) == XMAX_INVALID_ARGUMENT);
        window = nullptr;
        xmax_frame_release(frame);
        frame = nullptr;
        xmax_realtime_destroy(realtime);
        realtime = nullptr;
        std::cout << "Windows camera enumeration, HWND rendering and thread affinity passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (window)
            DestroyWindow(window);
        xmax_frame_release(frame);
        xmax_camera_list_release(cameras);
        xmax_realtime_destroy(realtime);
        xmax_client_destroy(client);
        std::cerr << error.what() << "; SDK: " << xmax_last_error() << '\n';
        return 1;
    }
}
