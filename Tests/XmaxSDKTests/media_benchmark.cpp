#include "Foundation/Media/FrameConverter.hpp"
#include "Media/Camera/CameraController.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace xmax::detail;

namespace {
/// @brief Compare native capture and unscaled SDK delivery for one explicitly named camera.
void camera(const std::string& name, int duration_seconds) {
    auto native = make_camera_capture();
    std::vector<CameraDevice> matches;
    for (const auto& device : native->devices())
        if (device.name.find(name) != std::string::npos)
            matches.push_back(device);
    require(matches.size() == 1, XMAX_INVALID_ARGUMENT, "Camera name must match exactly one device");
    const auto& selected = matches.front();
    for (bool pipeline : {false, true}) {
        std::atomic<bool> measuring{false};
        std::atomic<bool> ready{false};
        std::atomic<uint64_t> count{0};
        std::atomic<uint32_t> width{0}, height{0};
        std::mutex mutex;
        std::string failure;
        CaptureCallbacks callbacks;
        callbacks.frame = [&](Frame frame) {
            width = frame.width;
            height = frame.height;
            ready = true;
            if (measuring)
                ++count;
        };
        callbacks.failure = [&](Error error) {
            std::lock_guard<std::mutex> lock(mutex);
            failure = error.what();
        };
        CameraController pipelined;
        const std::unique_ptr<CameraCapture, void (*)(CameraCapture*)> stop_native(
            native.get(), [](CameraCapture* value) { value->stop(); });
        const auto cancel = [] { return false; };
        if (pipeline)
            pipelined.start(selected.id, {1920, 1080, 30}, callbacks, cancel);
        else
            native->start(selected.id, {1920, 1080, 30}, callbacks, cancel);
        // Both measurements exclude camera startup and wait for a real first frame.
        const auto deadline = Clock::now() + std::chrono::seconds(12);
        while (!ready) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                require(failure.empty(), XMAX_MEDIA_ERROR, failure);
            }
            require(Clock::now() < deadline, XMAX_TIMEOUT, "Camera produced no first frame");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto begin = Clock::now();
        measuring = true;
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
        measuring = false;
        const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        pipelined.stop();
        native->stop();
        std::lock_guard<std::mutex> lock(mutex);
        require(failure.empty(), XMAX_MEDIA_ERROR, failure);
        require(count > 0, XMAX_MEDIA_ERROR, "Camera produced no measured frames");
        std::cout << (pipeline ? "sdk_camera" : "native_camera") << ": size=" << width << 'x' << height
                  << " frames=" << count << " seconds=" << seconds << " fps=" << count / seconds << '\n';
    }
}
} // namespace

/// @brief Run an opt-in media benchmark; camera capture requires --camera and an explicit name filter.
int main(int argc, char** argv) {
    try {
        const int duration = argc > 1 ? std::clamp(std::stoi(argv[1]), 1, 30) : 5;
        if (argc == 4 && std::string(argv[2]) == "--camera")
            camera(argv[3], duration);
        else
            std::cout << "Usage: xmax_media_benchmark <seconds> --camera <unique device name>\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
