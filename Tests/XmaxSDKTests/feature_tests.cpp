#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Foundation/Media/FrameConverter.hpp"
#include "Stream/Quality/QualityController.hpp"
#include "TestSupport.hpp"
#include <iostream>
struct CaptureProbe {
    std::atomic<int> starts{0}, stops{0};
    bool fail = false, block = false;
    CaptureCallbacks callbacks;
    std::string device;
};
static Frame captured(int64_t timestamp) {
    Frame frame;
    frame.width = 1920;
    frame.height = 1024;
    frame.timestamp_us = timestamp;
    frame.strides[0] = frame.width * 4;
    frame.planes[0].assign(frame.strides[0] * frame.height, 42);
    return frame;
}
struct FakeCamera : CameraCapture {
    std::shared_ptr<CaptureProbe> probe;
    bool active = false;
    explicit FakeCamera(std::shared_ptr<CaptureProbe> value) : probe(std::move(value)) {}
    ~FakeCamera() override { stop(); }
    std::vector<CameraDevice> devices() override {
        return {{"one", "Camera one", true}, {"two", "Camera two", false}};
    }
    void start(const std::string& id, xmax_video_format, CaptureCallbacks callbacks,
               const Cancel& cancel) override {
        probe->device = id;
        probe->callbacks = std::move(callbacks);
        active = true;
        ++probe->starts;
        while (probe->block && !cancel())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(!cancel(), XMAX_CANCELLED, "Capture cancelled");
        require(!probe->fail, XMAX_MEDIA_ERROR, "Camera is unavailable");
        probe->callbacks.frame(captured(1));
    }
    void stop() noexcept override {
        if (active) {
            active = false;
            ++probe->stops;
        }
    }
};
static void camera_lifecycle() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    auto capture = std::make_shared<CaptureProbe>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(
        std::make_shared<ApiService>(config, http), [rtc] { return rtc; }, std::chrono::seconds(10),
        [capture] { return std::make_unique<FakeCamera>(capture); });
    sdk.prepare_camera("one", {1920, 1024, 30}, true);
    CHECK(sdk.state_snapshot().state == XMAX_READY && http->calls.empty());
    auto held = sdk.acquire_local_frame();
    CHECK(held && held->planes[0][0] == 42);
    CHECK(!sdk.requesting());
    sdk.connect(false);
    CHECK(rtc->microphone && rtc->joins == 1);
    CHECK(sdk.state_snapshot().state == XMAX_CONNECTED);
    sdk.start("first", "https://ref", true, false);
    const auto first_task = std::string(sdk.state_snapshot().task_id);
    capture->callbacks.frame(captured(2));
    CHECK(rtc->pushes == 1);
    sdk.volume(.6f);
    sdk.switch_camera("two");
    CHECK(capture->device == "two" && capture->starts == 2 && capture->stops == 1);
    CHECK(rtc->joins == 1 && http->count("POST /session") == 1);
    CHECK(first_task != sdk.state_snapshot().task_id && sdk.state_snapshot().state == XMAX_GENERATING);
    CHECK(sdk.volume() == .6f);
    expect_error(XMAX_INVALID_STATE, [&] { sdk.connect(false); });
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    sdk.disconnect(false);
    CHECK(capture->stops == 1 && sdk.state_snapshot().state == XMAX_READY);
    capture->callbacks.frame(captured(3));
    CHECK(sdk.acquire_local_frame());
    sdk.generate(nullptr);
    CHECK(http->count("POST /session") == 2);
    sdk.disconnect(true);
    CHECK(capture->stops == 2 && !sdk.acquire_local_frame());
    CHECK(held->planes[0][0] == 42);
}
static void camera_rollback_and_cancel() {
    auto http = std::make_shared<FakeHttp>();
    auto capture = std::make_shared<CaptureProbe>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(
        std::make_shared<ApiService>(config, http), [] { return std::make_shared<FakeRtc>(); },
        std::chrono::seconds(10), [capture] { return std::make_unique<FakeCamera>(capture); });
    capture->fail = true;
    expect_error(XMAX_MEDIA_ERROR, [&] { sdk.prepare_camera("one", {1920, 1024, 30}, false); });
    CHECK(sdk.state_snapshot().state == XMAX_IDLE && capture->stops == 1 && http->calls.empty());
    capture->fail = false;
    capture->block = true;
    auto start = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] { sdk.prepare_camera("one", {1920, 1024, 30}, false); });
    });
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (capture->starts < 2 && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(capture->starts == 2);
    sdk.disconnect(true);
    start.get();
    CHECK(sdk.state_snapshot().state == XMAX_IDLE && capture->stops == 2 && http->calls.empty());
}
static void separate_connect_and_generation() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    expect_error(XMAX_INVALID_ARGUMENT, [&] { sdk.prepare({1280, 720, 30}); });
    sdk.prepare({1920, 1024, 30});
    sdk.connect(false);
    CHECK(rtc->messages_count() == 0 && sdk.state_snapshot().state == XMAX_CONNECTED);
    xmax_generation_context context{sizeof(context), "hello", "https://ref", 1};
    sdk.generate(&context);
    const auto task = std::string(sdk.state_snapshot().task_id);
    sdk.generate(nullptr);
    CHECK(task == sdk.state_snapshot().task_id);
    CHECK(http->count("POST /session") == 1 && rtc->joins == 1);
    sdk.encoding({sizeof(xmax_encoding_config), 0, 2500, XMAX_ENCODER_MAINTAIN_QUALITY});
    CHECK(rtc->encoding.maximum_bitrate == 2500 && rtc->encoding.minimum_bitrate == 0);
    expect_error(XMAX_INVALID_ARGUMENT, [&] { sdk.encoding({sizeof(xmax_encoding_config), 500, 100, 0}); });
    expect_error(XMAX_INVALID_ARGUMENT, [&] { sdk.encoding({sizeof(xmax_encoding_config), -1, 50, 0}); });
    CHECK(rtc->encoding.maximum_bitrate == 2500);
    sdk.encoding({sizeof(xmax_encoding_config), 10000, 0, XMAX_ENCODER_AUTO});
    CHECK(rtc->encoding.maximum_bitrate >= rtc->encoding.minimum_bitrate);
    sdk.adaptive(false);
    CHECK(!sdk.quality().adaptive_enabled);
    expect_error(XMAX_UNSUPPORTED, [&] { sdk.interpolation(true); });
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    expect_error(XMAX_INVALID_STATE, [&] { sdk.stop_media(); });
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    sdk.disconnect(true);
}
static void conversion() {
    Frame source;
    source.width = 4;
    source.height = 2;
    source.timestamp_us = 7;
    source.format = XMAX_NV12;
    source.strides[0] = 4;
    source.strides[1] = 4;
    source.planes[0].assign(8, 235);
    source.planes[1].assign(4, 128);
    auto rgb = bgra_frame(source);
    CHECK(rgb.planes[0][0] >= 254 && rgb.planes[0][3] == 255);
    auto fit = fit_frame(rgb, {4, 4, 30});
    CHECK(fit.width == 4 && fit.height == 4 && fit.timestamp_us == 7);
    CHECK(fit.planes[0][0] == 0 && fit.planes[0][fit.strides[0]] >= 254);
    source.planes[1].clear();
    expect_error(XMAX_INVALID_ARGUMENT, [&] { bgra_frame(source); });
    expect_error(XMAX_INVALID_ARGUMENT, [&] { fit_frame(Frame{}, {4, 4, 30}); });
}
static void quality_policy() {
    QualityDirection policy;
    CHECK(!policy.observe(4, 1, 1));
    CHECK(!policy.observe(4, 2, 2));
    CHECK(policy.observe(4, 3, 3) == 3);
    policy.complete(3, true, 3);
    CHECK(!policy.observe(4, 4, 4));
    CHECK(!policy.observe(4, 5, 5));
    CHECK(!policy.observe(4, 6, 6));
    for (int t = 7; t < 11; ++t)
        CHECK(!policy.observe(1, t, t));
    CHECK(policy.observe(1, 11, 11) == 2);
    policy.complete(2, true, 11);
    CHECK(!policy.observe(4, 15, 15));
    CHECK(!policy.observe(4, 16, 16));
    CHECK(policy.observe(4, 17, 17) == 3);
    policy.complete(3, true, 17);
    for (int t = 21; t <= 27; ++t)
        CHECK(!policy.observe(1, t, t));
    CHECK(!policy.observe(1, 28, 40)); // stale sample
    CHECK(!policy.observe(4, 28, 40)); // duplicate sample
    CHECK(quality_format({1920, 1024, 30}, 2, true).width == 1500);
    CHECK(quality_format({1920, 1024, 30}, 2, false).width == 1530);
    auto rtc = std::make_shared<FakeRtc>();
    StreamController stream([rtc] { return rtc; });
    stream.allocate();
    const auto cancel = [] { return false; };
    stream.join({"a", "r", "u", "t", "b"}, {1920, 1024, 30}, false, {}, cancel);
    stream.start("p", "", false, cancel);
    double stamp = std::chrono::duration<double>(Clock::now().time_since_epoch()).count() - .1;
    CHECK(stream.observe_quality(1, 4, stamp, cancel).empty());
    CHECK(stream.observe_quality(1, 4, stamp + .001, cancel).empty());
    auto events = stream.observe_quality(1, 4, stamp + .002, cancel);
    CHECK(events.size() == 1 && events[0].quality == 3);
    CHECK(stream.quality().downlink_level == 3);
    CHECK(json::parse(rtc->messages.back())["event"] == "change_target_size");
    stream.adaptive(false);
    CHECK(stream.observe_quality(0, 4, stamp + .003, cancel).empty());
}
static void publication_timing() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    sdk.prepare({1920, 1024, 30});
    sdk.start("test", "", false, false);
    xmax_event event{};
    bool initial = false;
    while (sdk.poll(event))
        if (event.type == XMAX_EVENT_TIMING) {
            CHECK(event.publish_ms == -1);
            initial = true;
        }
    CHECK(initial && bool(rtc->callbacks.published));
    const auto old_publication = rtc->callbacks.published;
    rtc->callbacks.published();
    sdk.state_snapshot();
    bool measured = false;
    while (sdk.poll(event))
        if (event.type == XMAX_EVENT_TIMING) {
            CHECK(event.publish_ms >= 0);
            measured = true;
        }
    CHECK(measured);
    rtc->callbacks.published();
    sdk.state_snapshot();
    CHECK(!sdk.poll(event));
    sdk.disconnect(false);
    sdk.generate(nullptr);
    old_publication();
    sdk.state_snapshot();
    bool restarted = false;
    while (sdk.poll(event))
        if (event.type == XMAX_EVENT_TIMING) {
            CHECK(event.publish_ms == -1);
            restarted = true;
        }
    CHECK(restarted);
    sdk.disconnect(true);
}
static void async_exception_boundary() {
    std::atomic<int> failures{0};
    RealtimeCoordinator coordinator([] {},
                                    [&](Error error) {
                                        CHECK(error.code == XMAX_INTERNAL_ERROR);
                                        ++failures;
                                    });
    coordinator.enqueue([] { throw std::runtime_error("Internal asynchronous failure"); });
    coordinator.sync([&] { CHECK(failures == 1); });
    expect_error(XMAX_INVALID_STATE,
                 [&] { coordinator.sync([] { throw Error(XMAX_INVALID_STATE, "Caller error"); }); });
    coordinator.sync([&] { CHECK(failures == 1); });
}
int main() {
    int failures = 0;
    for (auto test : std::vector<std::pair<const char*, void (*)()>>{
             {"SDK camera lifecycle", camera_lifecycle},
             {"camera rollback and cancellation", camera_rollback_and_cancel},
             {"separate connection and generation", separate_connect_and_generation},
             {"CPU conversion and contain", conversion},
             {"adaptive quality policy", quality_policy},
             {"async exception containment", async_exception_boundary},
             {"actual publication timing and stale callback isolation", publication_timing}}) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << e.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
