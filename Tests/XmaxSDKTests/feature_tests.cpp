#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Foundation/Media/FrameConverter.hpp"
#include "Stream/Quality/QualityController.hpp"
#include "TestSupport.hpp"
#include <future>
#include <limits>
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
    CHECK(sdk.format().width == 1920 && sdk.format().height == 1024);
    auto held = sdk.acquire_local_frame();
    CHECK(held && held->planes[0][0] == 42);
    CHECK(!sdk.requesting());
    sdk.connect(false);
    CHECK(rtc->microphone && rtc->joins == 1);
    CHECK(sdk.state_snapshot().state == XMAX_CONNECTED);
    sdk.start("first", "https://ref", true, false);
    CHECK(json::parse(rtc->messages.front())["params"]["size"] == json::array({1920, 1024}));
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
    sdk.prepare({1280, 720, 30});
    CHECK(sdk.format().width == 1280);
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
    sdk.encoding({sizeof(xmax_encoding_config), -1, 50, 0});
    CHECK(rtc->encoding.minimum_bitrate == 50 && rtc->encoding.maximum_bitrate == 50);
    sdk.encoding({sizeof(xmax_encoding_config), 0, 2500, XMAX_ENCODER_MAINTAIN_QUALITY});
    CHECK(rtc->encoding.maximum_bitrate == 2500);
    expect_error(XMAX_INVALID_ARGUMENT, [&] { sdk.encoding({sizeof(xmax_encoding_config), 10000, 0, XMAX_ENCODER_AUTO}); });
    CHECK(rtc->encoding.maximum_bitrate == 2500 && rtc->encoding.minimum_bitrate == 0);
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
    CHECK(rgb.width == 4 && rgb.height == 2 && rgb.timestamp_us == 7);
    source.planes[1].clear();
    expect_error(XMAX_INVALID_ARGUMENT, [&] { bgra_frame(source); });
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
    CHECK(quality_format({1920, 1024, 30}, 2, true).width == 1680);
    CHECK(quality_format({1920, 1024, 30}, 2, false).width == 1680);
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
static void playback_policy_before_join() {
    struct Probe : FakeRtc {
        void join(const Connection& connection, VideoEncodingConfiguration format, bool mic,
                  RtcCallbacks callbacks, const Cancel& cancel) override {
            CHECK(remote_volume == .25f);
            FakeRtc::join(connection, format, mic, std::move(callbacks), cancel);
        }
    };
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<Probe>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    sdk.prepare({1920, 1024, 30});
    sdk.volume(.25f);
    sdk.connect(false);
    CHECK(sdk.volume() == .25f);
    sdk.disconnect(true);
}

/**
 * Checks that playback volume completes while RTC join remains blocked.
 */
static void playback_volume_during_startup() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    sdk.prepare({1920, 1024, 30});
    rtc->block_join = true;

    auto startup = std::async(std::launch::async, [&] { sdk.start("test", "", false, false); });
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!rtc->joins && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool joining = rtc->joins != 0;
    if (!joining) {
        rtc->block_join = false;
        startup.get();
        CHECK(joining);
    }

    std::promise<void> submitted;
    auto volume = std::async(std::launch::async, [&] {
        submitted.set_value();
        sdk.volume(.75f);
        return sdk.volume();
    });
    submitted.get_future().wait();
    const auto pending = volume.wait_for(std::chrono::seconds(1));
    rtc->block_join = false;
    startup.get();
    const auto retained = volume.get();

    CHECK(pending == std::future_status::ready && retained == .75f);
    CHECK(sdk.volume() == .75f && rtc->remote_volume == .75f);
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    xmax_event event{};
    while (sdk.poll(event))
        CHECK(event.type != XMAX_EVENT_ERROR);
    sdk.disconnect(true);
}

/**
 * Checks that result-frame waiting does not block reading or changing playback volume.
 */
static void playback_volume_during_first_frame_wait() {
    /**
     * An adapter whose first generated frame is released explicitly by the test.
     */
    struct Probe : FakeRtc {
        /**
         * Whether result-frame delivery remains suspended.
         */
        std::atomic<bool> held{true};

        /**
         * Whether generation has reached the result-frame wait.
         */
        std::atomic<bool> waiting{false};

        /**
         * Waits for explicit release while preserving cancellation and keepalive behavior.
         *
         * @param[in] cancel The operation cancellation predicate.
         * @param[in] keepalive The room and server maintenance callback.
         */
        void wait_remote(
            const Cancel& cancel,
            const std::function<void()>& keepalive
        ) override {
            waiting = true;
            while (held && !cancel()) {
                keepalive();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            FakeRtc::wait_remote(cancel, keepalive);
        }
    };
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<Probe>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    sdk.prepare({1920, 1024, 30});
    auto startup = std::async(std::launch::async, [&] { sdk.start("test", "", false, false); });
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!rtc->waiting && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool waiting = rtc->waiting;
    auto volume = std::async(std::launch::async, [&] {
        sdk.volume(.4f);
        return sdk.volume();
    });
    const auto completed = volume.wait_for(std::chrono::seconds(1));
    rtc->held = false;
    startup.get();
    const auto retained = volume.get();

    CHECK(waiting && completed == std::future_status::ready && retained == .4f);
    CHECK(rtc->remote_volume == .4f);
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    sdk.disconnect(true);
}

/**
 * Checks that callback draining detaches playback updates and that the next adapter receives the latest value.
 */
static void playback_volume_during_room_release() {
    /**
     * An adapter whose callback drain is released explicitly by the test.
     */
    struct Probe : FakeRtc {
        /**
         * Whether room shutdown has entered callback draining.
         */
        std::atomic<bool> draining{false};

        /**
         * Whether callback draining remains suspended.
         */
        std::atomic<bool> held{true};

        /**
         * The number of volume configurations received by this adapter.
         */
        std::atomic<int> volume_calls{0};

        /**
         * Records a playback configuration.
         *
         * @param[in] value The normalized playback volume.
         */
        void volume(
            float value
        ) override {
            ++volume_calls;
            FakeRtc::volume(value);
        }

        /**
         * Blocks callback draining until explicitly released.
         */
        void leave() noexcept override {
            draining = true;
            while (held)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            FakeRtc::leave();
        }
    };
    auto first = std::make_shared<Probe>();
    auto second = std::make_shared<FakeRtc>();
    int allocations = 0;
    StreamController stream([&]() -> std::shared_ptr<Rtc> { return allocations++ == 0 ? first : second; });
    stream.volume(.25f);
    stream.allocate();
    stream.join({"a", "r", "u", "t", "b"}, {1920, 1024, 30}, false, {}, [] { return false; });
    const auto previous_calls = first->volume_calls.load();
    auto stop = std::async(std::launch::async, [&] { stream.stop(); });
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!first->draining && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool draining = first->draining;
    auto volume = std::async(std::launch::async, [&] {
        stream.volume(.55f);
        return stream.volume();
    });
    const auto completed = volume.wait_for(std::chrono::seconds(1));
    first->held = false;
    stop.get();
    const auto retained = volume.get();

    CHECK(draining && completed == std::future_status::ready && retained == .55f);
    CHECK(first->volume_calls == previous_calls && first->remote_volume == .25f);
    stream.allocate();
    stream.join({"a", "r", "u", "t", "b"}, {1920, 1024, 30}, false, {}, [] { return false; });
    CHECK(second->remote_volume == .55f);
    stream.volume(.8f);
    CHECK(second->remote_volume == .8f && first->volume_calls == previous_calls);
    stream.stop();
}

/**
 * Checks volume validation and native failures without replacing the active generation or last value.
 */
static void playback_volume_failure_preserves_generation() {
    /**
     * A native playback adapter that can reject volume updates.
     */
    struct Probe : FakeRtc {
        /**
         * Whether native volume updates should fail.
         */
        bool reject = false;

        /**
         * Applies playback volume unless the native adapter is configured to reject it.
         *
         * @param[in] value The normalized playback volume.
         */
        void volume(
            float value
        ) override {
            require(!reject, XMAX_RTC_ERROR, "Native volume update rejected");
            FakeRtc::volume(value);
        }
    };
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<Probe>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    sdk.prepare({1920, 1024, 30});
    sdk.start("test", "", false, false);
    sdk.volume(.25f);

    for (const auto invalid : {-.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()})
        expect_error(XMAX_INVALID_ARGUMENT, [&] { sdk.volume(invalid); });
    rtc->reject = true;
    expect_error(XMAX_RTC_ERROR, [&] { sdk.volume(.75f); });
    CHECK(sdk.volume() == .25f && rtc->remote_volume == .25f);
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);

    rtc->reject = false;
    sdk.volume(0);
    CHECK(rtc->remote_volume == 0);
    sdk.volume(1);
    CHECK(rtc->remote_volume == 1);
    sdk.disconnect(true);
}
static void long_signaling_keepalive() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager sdk(
        std::make_shared<ApiService>(config, http), [rtc] { return rtc; }, std::chrono::milliseconds(100));
    sdk.prepare({1920, 1024, 30});
    sdk.start(std::string(10000, 'p'), "", false, false);
    CHECK(http->count("PUT /session/session-1/heartbeat") >= 2);
    bool room_heartbeat = false;
    for (const auto& packet : rtc->messages)
        if (json::parse(packet).value("event", "") == "heartbeat")
            room_heartbeat = true;
    CHECK(room_heartbeat && sdk.state_snapshot().state == XMAX_GENERATING);
    sdk.disconnect(true);
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
    XmaxRealtimeLifecycleManager coordinator([] {},
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
             {"actual publication timing and stale callback isolation", publication_timing},
             {"dual keepalive during long signaling", long_signaling_keepalive},
             {"playback policy applied before subscription", playback_policy_before_join},
             {"playback volume independent of startup", playback_volume_during_startup},
             {"playback volume independent of result-frame waits", playback_volume_during_first_frame_wait},
             {"playback volume independent of RTC callback draining", playback_volume_during_room_release},
             {"playback volume failure preserves generation", playback_volume_failure_preserves_generation}}) {
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
