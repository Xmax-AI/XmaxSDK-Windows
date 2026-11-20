//
//  camera_creation_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//
#include "CAPI/RealtimeHandle.hpp"
#include "Foundation/Media/Camera/CameraCaptureFormat.hpp"
#include "TestSupport.hpp"
#include <future>
#include <iostream>
#include <limits>
using namespace std::chrono_literals;

struct CaptureState {
    std::vector<CameraDevice> devices{{"rear", "Rear", true, XMAX_CAMERA_POSITION_BACK},
        {"front", "Front", false, XMAX_CAMERA_POSITION_FRONT}, {"usb", "USB", false}};
    std::string selected;
    xmax_video_format requested{};
    std::atomic<int> starts{0};
    CaptureCallbacks callbacks;
    Frame source;
};

static Frame pixels(uint32_t width = 4, uint32_t height = 2) {
    Frame frame;
    frame.width = width; frame.height = height; frame.timestamp_us = 1;
    frame.strides[0] = width * 4;
    frame.planes[0].resize(size_t(width) * height * 4);
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        frame.planes[0][i * 4] = uint8_t(i + 1);
        frame.planes[0][i * 4 + 3] = 255;
    }
    return frame;
}

struct Camera : CameraCapture {
    std::shared_ptr<CaptureState> state;
    explicit Camera(std::shared_ptr<CaptureState> value) : state(std::move(value)) {}
    std::vector<CameraDevice> devices() override { return state->devices; }
    void start(const std::string& device, xmax_video_format format, CaptureCallbacks output, const Cancel&) override {
        state->selected = device; state->requested = format; state->callbacks = output;
        output.frame(state->source);
        ++state->starts;
    }
    void stop() noexcept override { state->callbacks = {}; }
};

static void rotated_capture_geometry() {
    CHECK(camera_capture_distance(3840, 2160, 30, {3840, 2160, 30}) == 0);
    CHECK(camera_capture_distance(1280, 720, 30, {1280, 720, 30}) <
        camera_capture_distance(1920, 1080, 30, {1280, 720, 30}));
    for (unsigned rotation : {0u, 90u, 180u, 270u}) {
        auto state = std::make_shared<CaptureState>(); state->source = pixels();
        CameraController camera([state] { return std::make_unique<Camera>(state); });
        Frame result;
        const auto quarter = rotation == 90 || rotation == 270;
        CameraProcessingOptions options; options.rotation = rotation;
        camera.start("front", {quarter ? 2u : 4u, quarter ? 4u : 2u, 30},
            {[&](Frame frame) { result = std::move(frame); }, [](Error error) { throw error; }}, [] { return false; }, true, options);
        CHECK(state->requested.width == 4 && state->requested.height == 2);
        const std::vector<uint8_t> expected = rotation == 0 ? std::vector<uint8_t>{1,2,3,4,5,6,7,8}
            : rotation == 90 ? std::vector<uint8_t>{5,1,6,2,7,3,8,4}
            : rotation == 180 ? std::vector<uint8_t>{8,7,6,5,4,3,2,1} : std::vector<uint8_t>{4,8,3,7,2,6,1,5};
        for (size_t i = 0; i < expected.size(); ++i) CHECK(result.planes[0][i * 4] == expected[i]);
    }
    auto state = std::make_shared<CaptureState>(); state->source = pixels();
    CameraController camera([state] { return std::make_unique<Camera>(state); });
    CameraProcessingOptions options; options.capture_format = xmax_video_format{3840, 2160, 24}; options.rotation = 90;
    Frame result;
    camera.start("front", {2, 4, 30}, {[&](Frame frame) { result = std::move(frame); }, [](Error error) { throw error; }},
        [] { return false; }, true, options);
    CHECK(state->requested.width == 3840 && state->requested.height == 2160 && state->requested.fps == 24);
    CHECK(result.width == 2 && result.height == 4 && result.planes[0][0] == 5);
    camera.stop();
    options.rotation = 0; options.capture_format = xmax_video_format{4, 2, 120}; options.limit_output_rate = true;
    int delivered = 0;
    camera.start("front", {4, 2, 30}, {[&](Frame) { ++delivered; }, [](Error error) { throw error; }},
        [] { return false; }, true, options);
    for (int64_t timestamp : {8334, 16667, 25000, 33334, 41667, 50000, 66667}) {
        auto frame = pixels(); frame.timestamp_us = timestamp; state->callbacks.frame(std::move(frame));
    }
    CHECK(delivered == 3);
}

struct Fixture {
    std::shared_ptr<CaptureState> camera = std::make_shared<CaptureState>();
    std::shared_ptr<FakeHttp> http = std::make_shared<FakeHttp>();
    std::shared_ptr<FakeRtc> rtc = std::make_shared<FakeRtc>();
    std::unique_ptr<xmax_realtime_impl> handle;
    Fixture() {
        camera->source = pixels(640, 480);
        Config config; config.key = "offline-test";
        auto service = std::make_shared<ApiService>(config, http);
        handle = std::make_unique<xmax_realtime_impl>(std::make_unique<XmaxRealtimeManager>(service,
            [value = rtc] { return value; }, 10s, [value = camera] { return std::make_unique<Camera>(value); }));
    }
    xmax_camera_stream_options options() {
        xmax_camera_stream_options options{}; options.struct_size = sizeof(options);
        options.camera_position = XMAX_CAMERA_POSITION_FRONT; options.mirror = -1;
        options.video_format = {1920, 1080, 30};
        return options;
    }
};

static void creation_defaults_and_retained_settings() {
    Fixture f;
    f.handle->session->volume(.75f);
    auto options = f.options();
    options.capture_width = 3840; options.capture_height = 2160;
    const auto created = xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options);
    if (created != XMAX_OK) std::cerr << "Creation result " << created << ": " << xmax_last_error() << '\n';
    CHECK(created == XMAX_OK);
    CHECK(f.camera->selected == "front" && f.camera->requested.width == 3840 && f.camera->requested.height == 2160);
    CHECK(f.camera->requested.fps == 30 && f.handle->session->format().width == 1920);
    CHECK(f.handle->session->volume() == .75f && !f.handle->session->microphone_enabled());
    CHECK(f.http->calls.empty() && f.rtc->joins == 0);
    CHECK(f.handle->session->state_snapshot().state == XMAX_PREPARING);
    CHECK(f.handle->session->video_mirror().local_preview && f.handle->session->video_mirror().published_video);
    f.handle->session->complete_camera_preview(f.handle->session->local_stream_revision());
    CHECK(f.handle->session->state_snapshot().state == XMAX_READY);
    CHECK(xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options) == XMAX_INVALID_STATE);
    CHECK(f.camera->starts == 1 && f.handle->session->state_snapshot().state == XMAX_READY);
    f.handle->session->switch_camera("rear");
    CHECK(!f.handle->session->video_mirror().local_preview);
    CHECK(f.camera->requested.width == 3840 && f.camera->requested.height == 2160);
    f.handle->session->update_camera_format({1536, 1152, 30});
    CHECK(f.camera->requested.width == 3840 && f.handle->session->format().width == 1536);
    CHECK(f.handle->session->volume() == .75f);
}

static void selection_and_validation() {
    for (const auto id : {"rear", "front", "usb"}) {
        Fixture f; auto options = f.options(); options.device_id = id;
        CHECK(xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options) == XMAX_OK);
        CHECK(bool(f.handle->session->video_mirror().local_preview) == (std::string(id) == "front"));
    }
    { Fixture f; f.camera->devices = {{"usb", "USB", true}}; auto options = f.options(); options.device_id = "usb";
      CHECK(xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options) == XMAX_OK);
      CHECK(f.handle->session->video_mirror().local_preview); }
    { Fixture f; auto options = f.options(); options.camera_position = XMAX_CAMERA_POSITION_BACK; options.mirror = 1;
      CHECK(xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options) == XMAX_OK);
      CHECK(f.camera->selected == "rear" && f.handle->session->video_mirror().local_preview);
      f.handle->session->switch_camera("usb"); CHECK(f.handle->session->video_mirror().local_preview); }
    Fixture f;
    const auto invalid = [&](auto modify) {
        auto options = f.options(); modify(options);
        CHECK(xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options) == XMAX_INVALID_ARGUMENT);
        CHECK(f.camera->starts == 0 && f.http->calls.empty());
        CHECK(f.handle->session->state_snapshot().state == XMAX_IDLE);
    };
    invalid([](auto& o) { o.rotation = 45; });
    invalid([](auto& o) { o.camera_position = 7; });
    invalid([](auto& o) { o.mirror = 2; });
    invalid([](auto& o) { o.warmup_ms = -1; });
    invalid([](auto& o) { o.warmup_ms = std::numeric_limits<double>::quiet_NaN(); });
    invalid([](auto& o) { o.capture_width = 1920; });
    invalid([](auto& o) { o.capture_fps = -1; });
    invalid([](auto& o) { o.capture_fps = 30; });
    invalid([](auto& o) { o.device_id = ""; });
}

static void warmup_before_publication_and_cancellation() {
    Fixture f; auto options = f.options(); options.warmup_ms = 180;
    CHECK(xmax_realtime_create_local_camera_stream_with_options(f.handle.get(), &options) == XMAX_OK);
    const auto start = Clock::now(); f.handle->session->connect(false);
    CHECK(Clock::now() - start >= 170ms && f.rtc->joins == 1 && f.rtc->remote_volume == 0);
    f.handle->session->disconnect(false);
    f.handle->session->volume(.8f);
    const auto reconnect = Clock::now(); f.handle->session->connect(false);
    CHECK(Clock::now() - reconnect >= 170ms && f.rtc->remote_volume == .8f);
    Fixture cancelled; options.warmup_ms = 10000;
    CHECK(xmax_realtime_create_local_camera_stream_with_options(cancelled.handle.get(), &options) == XMAX_OK);
    auto pending = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] { cancelled.handle->session->connect(false); });
    });
    const auto entered = Clock::now() + 2s;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(cancelled.http->mutex);
            if (!cancelled.http->calls.empty()) break;
        }
        CHECK(Clock::now() < entered);
        std::this_thread::sleep_for(1ms);
    }
    const auto stop = Clock::now(); cancelled.handle->session->disconnect(false);
    CHECK(pending.wait_for(2s) == std::future_status::ready); pending.get();
    CHECK(Clock::now() - stop < 2s && cancelled.rtc->joins == 0);
    CHECK(cancelled.handle->session->state_snapshot().state == XMAX_READY);
}

int main() {
    try {
        rotated_capture_geometry(); creation_defaults_and_retained_settings(); selection_and_validation();
        warmup_before_publication_and_cancellation();
        std::cout << "Camera creation alignment tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
