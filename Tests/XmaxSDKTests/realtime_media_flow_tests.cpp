//
//  realtime_media_flow_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "TestSupport.hpp"
#include <condition_variable>
#include <iostream>

namespace {

using namespace std::chrono_literals;

Frame pixels(uint32_t width, uint32_t height, int64_t timestamp, int translation = 0) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.timestamp_us = timestamp;
    frame.strides[0] = width * 4;
    frame.planes[0].resize(frame.strides[0] * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto index = y * frame.strides[0] + x * 4;
            const auto sample = uint8_t(32 + ((int(x) - translation + int(y)) & 31) * 4);
            for (int channel = 0; channel < 3; ++channel)
                frame.planes[0][index + channel] = sample;
            frame.planes[0][index + 3] = 255;
        }
    }
    return frame;
}

struct Camera final : CameraCapture {
    std::vector<CameraDevice> devices() override {
        return {{"camera-one", "First camera", true}, {"camera-two", "Second camera", false}};
    }

    void start(
        const std::string& device,
        xmax_video_format,
        CaptureCallbacks callbacks,
        const Cancel&
    ) override {
        if (device == "missing")
            throw Error(XMAX_MEDIA_ERROR, "Requested camera is unavailable");
        callbacks.frame(pixels(32, 16, 1000));
    }

    void stop() noexcept override {}
};

struct AudioRtc final : FakeRtc {
    void microphone_enabled(bool enabled) override { microphone = enabled; }
};

void unpublished_before_first_frame() {
    struct UnpublishingRtc final : FakeRtc {
        void wait_remote(const Cancel&, Clock::time_point) override { callbacks.video_unpublished(); }
    };
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<UnpublishingRtc>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    sdk.prepare({1280, 720, 30});
    sdk.connect(false);
    const auto began = Clock::now();
    expect_error(XMAX_CANCELLED, [&] { sdk.start("test", "", false, false); });
    CHECK(Clock::now() - began < 3s);
}

template<class Predicate>
void wait_until(Predicate predicate) {
    const auto deadline = Clock::now() + 4s;
    while (!predicate()) {
        CHECK(Clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}

std::optional<json> last_event(FakeRtc& rtc, const char* event) {
    std::lock_guard<std::mutex> lock(rtc.mutex);
    for (auto item = rtc.messages.rbegin(); item != rtc.messages.rend(); ++item) {
        auto value = json::parse(*item);
        if (value["event"] == event)
            return value;
    }
    return {};
}

void camera_stream_generation_lifecycle() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<AudioRtc>();
    Config config;
    config.key = "test";
    std::vector<std::string> validated;
    XmaxRealtimeManager sdk(
        std::make_shared<ApiService>(config, http),
        [rtc] { return rtc; },
        10s,
        [] { return std::make_unique<Camera>(); },
        {},
        [&](const std::string& device) { validated.push_back(device); }
    );
    sdk.adaptive(false);
    const xmax_encoding_config encoding{sizeof(encoding), 600, 2500, XMAX_ENCODER_MAINTAIN_FRAMERATE};
    sdk.create_local_camera_stream("camera-one", {1920, 1080, 30}, true, false, "endpoint", true, encoding);
    const auto local = sdk.local_stream_revision();
    CHECK(validated == std::vector<std::string>{"endpoint"});
    CHECK(sdk.acquire_local_frame(local));
    CHECK(sdk.state_snapshot().state == XMAX_PREPARING);
    sdk.disconnect(false);
    CHECK(sdk.state_snapshot().state == XMAX_PREPARING);
    sdk.switch_camera("camera-two");
    xmax_video_format format{};
    std::optional<CameraDevice> device;
    CHECK(sdk.read_video_track(local, true, format, nullptr, &device));
    CHECK(device && device->id == "camera-two" && format.fps == 30);
    CHECK(sdk.state_snapshot().state == XMAX_PREPARING && sdk.local_stream_revision() == local);
    expect_error(XMAX_INVALID_STATE, [&] { sdk.switch_microphone("another"); });
    expect_error(XMAX_INVALID_STATE, [&] { sdk.set_microphone_enabled(false); });

    const auto generate = xmax_generation_context{sizeof(xmax_generation_context), "test", "", 0};
    expect_error(XMAX_INVALID_STATE, [&] { sdk.generate(&generate, 0, false, false); });
    CHECK(rtc->joins == 0 && sdk.state_snapshot().state == XMAX_PREPARING);

    const auto remote = sdk.connect(false, local);
    sdk.complete_camera_preview(local);
    CHECK(sdk.state_snapshot().state == XMAX_CONNECTED);
    CHECK(rtc->encoding.minimum_bitrate == 600 && rtc->encoding.maximum_bitrate == 2500);
    CHECK(rtc->microphone && sdk.microphone_enabled());
    sdk.switch_microphone("second");
    CHECK(rtc->selected_microphone == "second" && validated.back() == "second");
    rtc->callbacks.microphone_level(180);
    xmax_event event{};
    bool measured = false;
    while (sdk.poll(event))
        measured |= event.type == XMAX_EVENT_MICROPHONE_LEVEL && event.code == 180;
    CHECK(measured);
    sdk.set_microphone_enabled(false);
    CHECK(!rtc->microphone && !sdk.microphone_enabled());
    rtc->callbacks.microphone_level(255);
    bool cleared = false;
    while (sdk.poll(event)) {
        if (event.type == XMAX_EVENT_MICROPHONE_LEVEL) {
            CHECK(event.code == -1);
            cleared = true;
        }
    }
    CHECK(cleared);
    sdk.switch_microphone("");
    sdk.set_microphone_enabled(true);
    CHECK(rtc->selected_microphone.empty() && rtc->microphone && rtc->joins == 1);

    CHECK(sdk.generate(&generate, local, true) == remote);
    CHECK(rtc->joins == 1 && sdk.state_snapshot().state == XMAX_GENERATING);
    const auto existing_task = std::string(sdk.state_snapshot().task_id);
    rtc->auto_ack = false;
    const auto messages_before_update = rtc->messages_count();
    expect_error(XMAX_CANCELLED, [&] {
        sdk.generate(&generate, local, true, true, [&] { return rtc->messages_count() > messages_before_update; });
    });
    rtc->auto_ack = true;
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING && existing_task == sdk.state_snapshot().task_id);
    CHECK(rtc->leaves == 0 && sdk.generate(&generate, 0, false, false) == remote);


    const xmax_point point{25, 50};
    xmax_interaction_frame interaction{sizeof(interaction), &point, 1, 100, 100, 100, 100, XMAX_CONTENT_FIT};
    sdk.video_mirror({sizeof(xmax_video_mirror_config), 0, 1, 0});
    expect_error(XMAX_INVALID_STATE, [&] { sdk.submit_interaction(interaction, true, remote + 1); });
    sdk.submit_interaction(interaction, true, remote);
    wait_until([&] { return last_event(*rtc, "tracks").has_value(); });
    CHECK((*last_event(*rtc, "tracks"))["tracks"][0] == json::array({.75, .5}));
    CHECK(sdk.trajectory().front().x == .25);

    const auto task = std::string(sdk.state_snapshot().task_id);
    const auto downlink = sdk.quality().downlink;
    CHECK(downlink.width == 1920 && downlink.height == 1080);
    const auto start = last_event(*rtc, "start");
    CHECK(start && (*start)["params"]["target_size"] == json::array({1920, 1080}));
    CHECK((*start)["params"]["min_bitrate"] == 500);
    const auto update = last_event(*rtc, "change_condition");
    CHECK(update && !(*update)["params"].contains("target_size"));
    sdk.quality_level(1, 3);
    const auto target = sdk.quality().downlink;
    CHECK(sdk.read_video_track(remote, false, format, nullptr, nullptr));
    CHECK(format.width == target.width && format.height == target.height && format.width < downlink.width);
    CHECK(format.fps == 30);

    sdk.remote_video_beauty({});
    std::mutex received_mutex;
    std::vector<int64_t> received;
    sdk.media_listener([&](std::shared_ptr<const RemoteMediaFrame> frame) {
        if (frame->video) {
            std::lock_guard<std::mutex> lock(received_mutex);
            received.push_back(frame->video->timestamp_us);
        }
    });
    rtc->callbacks.frame(pixels(64, 32, 100000));
    wait_until([&] {
        std::lock_guard<std::mutex> lock(received_mutex);
        return received.size() == 1;
    });
    rtc->callbacks.frame(pixels(64, 32, 150000, 4));
    wait_until([&] {
        const auto frame = sdk.acquire_frame(remote);
        return frame && frame->timestamp_us == 150000;
    });
    wait_until([&] {
        std::lock_guard<std::mutex> lock(received_mutex);
        return received.size() >= 2;
    });
    {
        std::lock_guard<std::mutex> lock(received_mutex);
        CHECK((received == std::vector<int64_t>{100000, 150000}));
    }
    CHECK(task == sdk.state_snapshot().task_id && rtc->joins == 1);

    uint64_t content_revision = 0;
    CHECK(!sdk.acquire_result(remote, content_revision));
    rtc->callbacks.frame(pixels(64, 32, 200000));
    rtc->callbacks.video_unpublished();
    uint64_t unpublished_revision = 0;
    CHECK(!sdk.acquire_result(remote, unpublished_revision) && unpublished_revision != content_revision);
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    rtc->callbacks.frame(pixels(64, 32, 250000));
    CHECK(!sdk.acquire_result(remote, content_revision) && content_revision == unpublished_revision);

    const auto switching = Clock::now();
    sdk.switch_camera("camera-two");
    CHECK(Clock::now() - switching >= 500ms);
    CHECK(sdk.local_stream_revision() == local && sdk.acquire_local_frame(local));
    CHECK(sdk.acquire_result(remote, content_revision));
    expect_error(XMAX_MEDIA_ERROR, [&] { sdk.switch_camera("missing"); });
    CHECK(sdk.state_snapshot().state == XMAX_READY && !sdk.acquire_frame(remote));
    CHECK(sdk.local_stream_revision() == local && sdk.acquire_local_frame(local));
    sdk.stop_media(XmaxRealtimeManager::StopMediaSource::Camera);
    CHECK(!sdk.acquire_local_frame(local) && sdk.local_stream_revision() != local);
    expect_error(XMAX_INVALID_STATE, [&] { sdk.generate(&generate, local); });
    sdk.media_listener({});
    sdk.create_local_camera_stream("camera-two", {1920, 1080, 30}, false, false, {}, true);
    CHECK(sdk.state_snapshot().state == XMAX_PREPARING);
    CHECK(sdk.generate(&generate, sdk.local_stream_revision()) != 0);
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
}

} // namespace

int main() {
    try {
        camera_stream_generation_lifecycle();
        unpublished_before_first_frame();
        std::cout << "Realtime media flow checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
