#include "CAPI/RealtimeHandle.hpp"
#include "Render/Video/RemoteMediaFrameDispatcher.hpp"
#include "Render/Video/RemoteAudioTimeline.hpp"
#include "TestSupport.hpp"
#include <xmax/xmax.hpp>
#include <condition_variable>
#include <future>
#include <iostream>
#include <map>

using namespace std::chrono_literals;

static std::shared_ptr<ApiService> api(std::shared_ptr<Http> http = std::make_shared<FakeHttp>()) {
    Config config;
    config.key = "test";
    return std::make_shared<ApiService>(config, std::move(http));
}

template <class F> static void eventually(F condition) {
    const auto deadline = Clock::now() + 5s;
    while (!condition()) {
        CHECK(Clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}

struct MemoryPreferences : PreferenceStore {
    std::map<std::string, std::string> values;
    std::optional<std::string> read(const std::string& key) noexcept override {
        const auto found = values.find(key);
        return found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
    }
    void write(const std::string& key, const std::string& value) noexcept override { values[key] = value; }
};

static Frame video(int64_t time = 1000000, uint32_t width = 4, uint32_t height = 4) {
    Frame result;
    result.width = width;
    result.height = height;
    result.timestamp_us = time;
    result.strides[0] = size_t(width) * 4;
    result.planes[0].assign(size_t(width) * height * 4, 0x41);
    return result;
}

static AudioFrame audio(int64_t time = 1000, uint32_t rate = 48000, uint32_t samples = 480) {
    AudioFrame result;
    result.render_time_ms = time;
    result.sample_rate = rate;
    result.channels = 1;
    result.samples_per_channel = samples;
    result.data.assign(size_t(samples) * 2, 0x31);
    return result;
}

static void disabled_quality() {
    auto preferences = std::make_shared<MemoryPreferences>();
    auto history = std::make_shared<QualityHistory>("x2.1", preferences);
    history->write(4, false);
    history->write(5, true);
    auto rtc = std::make_shared<FakeRtc>();
    XmaxRealtimeManager manager(api(), [rtc] { return rtc; }, 10s, make_camera_capture, history);
    manager.adaptive(false);
    manager.prepare({640, 480, 30});
    manager.start("first", "", false, false);
    CHECK(!manager.quality().adaptive_enabled);
    CHECK(manager.quality().uplink_level == 1 && manager.quality().downlink_level == 1);
    manager.quality_level(0, 3);
    manager.quality_level(1, 2);
    CHECK(manager.quality().uplink_level == 3 && manager.quality().downlink_level == 2);
    CHECK(history->read(false) == 4 && history->read(true) == 5);
    manager.disconnect(false);
    manager.connect(false);
    CHECK(manager.quality().uplink_level == 1 && manager.quality().downlink_level == 1);
    manager.disconnect(false);
    manager.adaptive(true);
    manager.connect(false);
    CHECK(manager.quality().uplink_level == 4 && manager.quality().downlink_level == 5);
}

struct WaitingRtc : FakeRtc {
    std::atomic<int> waits{0};
    void wait_remote(const Cancel& cancel, const std::function<void()>& keepalive) override {
        if (++waits != 1)
            return;
        const auto deadline = Clock::now() + 5s;
        while (!cancel() && Clock::now() < deadline) {
            keepalive();
            std::this_thread::sleep_for(1ms);
        }
        throw Error(XMAX_CANCELLED, "Generation cancelled");
    }
};

static xmax_generation_context context(const char* prompt) {
    return {sizeof(xmax_generation_context), prompt, nullptr, 0};
}

static void generation_replacement() {
    auto rtc = std::make_shared<WaitingRtc>();
    auto http = std::make_shared<FakeHttp>();
    XmaxRealtimeManager manager(api(http), [rtc] { return rtc; });
    manager.prepare({640, 480, 30});
    manager.connect(false);
    auto old = std::async(std::launch::async, [&] {
        const auto value = context("old conditions");
        expect_error(XMAX_CANCELLED, [&] { manager.generate(&value); });
    });
    eventually([&] { return rtc->waits == 1; });
    const auto value = context("latest conditions");
    manager.generate(&value);
    CHECK(old.wait_for(5s) == std::future_status::ready);
    old.get();
    CHECK(manager.state_snapshot().state == XMAX_GENERATING);
    CHECK(rtc->joins == 1 && rtc->leaves == 0 && rtc->waits == 2);
    CHECK(http->count("POST /session") == 1);
    auto pixels = video(1000000, 640, 480);
    auto input = pixels.view();
    // Request replacement must not cancel the connection's publication callback.
    manager.push(input);
    CHECK(rtc->pushes == 1);
    manager.generate(nullptr);
    std::lock_guard<std::mutex> lock(rtc->mutex);
    CHECK(rtc->messages.back().find("latest conditions") != std::string::npos);
    CHECK(rtc->messages.back().find("old conditions") == std::string::npos);
}

static void replacement_during_connect() {
    auto first = std::make_shared<FakeRtc>();
    first->block_join = true;
    auto second = std::make_shared<FakeRtc>();
    std::atomic<int> created{0};
    auto http = std::make_shared<FakeHttp>();
    XmaxRealtimeManager manager(api(http), [&]() -> std::shared_ptr<Rtc> {
        return ++created == 1 ? first : second;
    });
    manager.prepare({640, 480, 30});
    auto old = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] { manager.start("old", "", false, false); });
    });
    eventually([&] { return first->joins == 1; });
    manager.start("latest", "", false, false);
    CHECK(old.wait_for(5s) == std::future_status::ready);
    old.get();
    CHECK(created == 2 && first->leaves == 1 && second->joins == 1);
    CHECK(http->count("DELETE /session/session-1") == 1);
    CHECK(manager.state_snapshot().state == XMAX_GENERATING);
}

static void explicit_disconnect_cancels_request() {
    auto rtc = std::make_shared<WaitingRtc>();
    XmaxRealtimeManager manager(api(), [rtc] { return rtc; });
    manager.prepare({640, 480, 30});
    manager.connect(false);
    auto pending = std::async(std::launch::async, [&] {
        const auto value = context("pending");
        expect_error(XMAX_CANCELLED, [&] { manager.generate(&value); });
    });
    eventually([&] { return rtc->waits == 1; });
    manager.disconnect(false);
    CHECK(pending.wait_for(5s) == std::future_status::ready);
    pending.get();
    CHECK(manager.state_snapshot().state == XMAX_READY);
    CHECK(rtc->joins == 1 && rtc->leaves == 1);
}

static void source_preview_revision() {
    ExternalVideoController source;
    source.prepare({4, 4, 30}, 1);
    int delivered = 0;
    source.preview([&](Frame) { ++delivered; }, 2);
    // A late old-source submit must not be routed through the replacement preview callback.
    source.push({}, 1);
    CHECK(delivered == 0);
    source.prepare({4, 4, 30}, 2);
    auto pixels = video();
    source.push(pixels.view(), 2);
    CHECK(delivered == 1);
}

struct FailingCamera : CameraCapture {
    std::vector<CameraDevice> devices() override { return {}; }
    void start(const std::string&, xmax_video_format, CaptureCallbacks callbacks, const Cancel&) override {
        callbacks.failure(Error(XMAX_MEDIA_ERROR, "native capture failure"));
    }
    void stop() noexcept override {}
};

static void camera_startup_failure_contract() {
    XmaxRealtimeManager manager(api(), [] { return std::make_shared<FakeRtc>(); }, 10s,
        [] { return std::make_unique<FailingCamera>(); });
    expect_error(XMAX_MEDIA_ERROR, [&] { manager.prepare_camera("", {1080, 1920, 30}, false); });
    const auto state = manager.state_info();
    CHECK(state.event.state == XMAX_IDLE && state.error.code == XMAX_MEDIA_ERROR);
    CHECK(std::string(state.error.message).find("native capture failure") != std::string::npos);
}

static void external_source_failures() {
    auto rtc = std::make_shared<WaitingRtc>();
    XmaxRealtimeManager manager(api(), [rtc] { return rtc; });
    const auto stale = manager.prepare({640, 480, 30});
    const auto active = manager.prepare({640, 480, 30});
    CHECK(active != stale);
    manager.push({}, stale);
    manager.external_failure(stale, Error(XMAX_MEDIA_ERROR, "stale capture"));
    CHECK(manager.state_info().reason == XMAX_REASON_NORMAL);
    manager.connect(false);
    CHECK(!manager.requesting(stale) && manager.requesting(active));
    auto pending = std::async(std::launch::async, [&] {
        const auto value = context("waiting for capture");
        expect_error(XMAX_CANCELLED, [&] { manager.generate(&value); });
    });
    eventually([&] { return rtc->waits == 1; });
    manager.external_failure(active, Error(XMAX_MEDIA_ERROR, "capture disconnected", 1234, 503));
    manager.external_failure(active, Error(XMAX_MEDIA_ERROR, "duplicate"));
    CHECK(pending.wait_for(5s) == std::future_status::ready);
    pending.get();
    const auto state = manager.state_info();
    CHECK(state.event.state == XMAX_IDLE && state.reason == XMAX_REASON_FAILURE);
    CHECK(state.error.code == XMAX_MEDIA_ERROR && state.error.api_code == 1234 && state.error.http_status == 503);
    int failures = 0;
    xmax_event_info event{};
    while (manager.poll(event))
        if (event.event.type == XMAX_EVENT_ERROR && event.event.code == XMAX_MEDIA_ERROR)
            ++failures;
    CHECK(failures == 1 && !manager.requesting(active));
    const auto latest = manager.prepare({640, 480, 30});
    CHECK(latest != active);
    manager.push({}, active);
    manager.external_failure(active, Error(XMAX_MEDIA_ERROR, "late capture failure"));
    CHECK(manager.state_info().event.state == XMAX_READY);
    CHECK(manager.state_info().reason == XMAX_REASON_NORMAL);
}

struct CollectedFrames {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::shared_ptr<const RemoteMediaFrame>> values;
    void add(std::shared_ptr<const RemoteMediaFrame> value) {
        std::lock_guard<std::mutex> lock(mutex);
        values.push_back(std::move(value));
        changed.notify_all();
    }
    std::vector<std::shared_ptr<const RemoteMediaFrame>> wait(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(changed.wait_for(lock, 5s, [&] { return values.size() >= count; }));
        return values;
    }
};

static long double seconds(xmax_media_time value) {
    CHECK(value.timescale > 0);
    return static_cast<long double>(value.value) / value.timescale;
}

static void audio_sample_timeline() {
    RemoteAudioTimeline timeline;
    xmax_media_time time{};
    bool discontinuous = false;
    auto packet = audio(1000, 44100, 1);
    for (int i = 0; i < 1000; ++i) {
        CHECK(timeline.resolve(packet, time, discontinuous));
        CHECK(discontinuous == (i == 0));
        CHECK(time.value == int64_t(1000) * 44100 + int64_t(i) * 1000);
        CHECK(time.timescale == 44100000);
    }
    packet.render_time_ms = 1200;
    CHECK(timeline.resolve(packet, time, discontinuous) && discontinuous);
    CHECK(time.value == int64_t(1200) * 44100);
    packet.data.pop_back();
    CHECK(!timeline.resolve(packet, time, discontinuous));
    packet = audio(1201, 48000, 480);
    CHECK(timeline.resolve(packet, time, discontinuous) && discontinuous);
    packet.render_time_ms = 0;
    CHECK(!timeline.resolve(packet, time, discontinuous));
    packet.render_time_ms = std::numeric_limits<int64_t>::max();
    CHECK(!timeline.resolve(packet, time, discontinuous));
}

static void shared_media_timeline() {
    CollectedFrames collected;
    RemoteMediaFrameDispatcher dispatcher;
    dispatcher.listener([&](auto frame) { collected.add(std::move(frame)); });
    dispatcher.bind(10);
    dispatcher.audio(audio(), 10);
    dispatcher.video(std::make_shared<Frame>(video()), 10);
    dispatcher.audio(audio(), 10); // Native callbacks may batch with repeated render times.
    dispatcher.audio(audio(1020), 10);
    auto values = collected.wait(4);
    CHECK(values[0]->type == XMAX_MEDIA_AUDIO && values[1]->type == XMAX_MEDIA_VIDEO);
    CHECK(values[0]->stream_id == values[1]->stream_id && values[0]->stream_id != 0);
    CHECK(values[0]->discontinuous && values[1]->discontinuous);
    CHECK(!values[2]->discontinuous && !values[3]->discontinuous);
    CHECK(seconds(values[0]->presentation_time) == seconds(values[1]->presentation_time));
    CHECK(std::fabs(seconds(values[2]->presentation_time) - 1.01L) < 0.0000001L);
    CHECK(std::fabs(seconds(values[3]->presentation_time) - 1.02L) < 0.0000001L);
    CHECK(seconds(values[0]->duration) == 0.01L && values[1]->duration.timescale == 0);
    const auto pcm = values[0]->view();
    CHECK(pcm.audio.data_size == 960 && pcm.audio.data[0] == 0x31 && pcm.video.width == 0);
    const auto pixels = values[1]->view();
    CHECK(pixels.video.planes[0][0] == 0x41 && pixels.audio.data == nullptr);

    dispatcher.video(std::make_shared<Frame>(video(1030000)), 10);
    dispatcher.video(std::make_shared<Frame>(video(1040000, 6)), 10);
    dispatcher.video(std::make_shared<Frame>(video(1040000, 6)), 10);
    dispatcher.audio(audio(2000), 10);
    dispatcher.audio({}, 10);
    dispatcher.audio(audio(2010), 10);
    values = collected.wait(9);
    CHECK(!values[4]->discontinuous);
    CHECK(values[5]->discontinuous && values[6]->discontinuous);
    CHECK(values[7]->discontinuous && values[8]->discontinuous);
    CHECK(seconds(values[7]->presentation_time) == 2.0L);

    dispatcher.bind(11);
    dispatcher.audio(audio(3000), 10);
    dispatcher.video(std::make_shared<Frame>(video()), 10);
    dispatcher.audio(audio(3000), 11);
    values = collected.wait(10);
    CHECK(values.size() == 10 && values[9]->stream_id != values[0]->stream_id);
    CHECK(values[9]->discontinuous);
    const auto current_stream = values[9]->stream_id;
    dispatcher.listener({});
    dispatcher.listener([&](auto frame) { collected.add(std::move(frame)); });
    dispatcher.audio(audio(3010), 11);
    values = collected.wait(11);
    CHECK(values[10]->stream_id == current_stream && values[10]->discontinuous);
    dispatcher.unbind(12);
    dispatcher.bind(11); // A stale bind must not reactivate a stopped result.
    dispatcher.audio(audio(3020), 11);
    dispatcher.bind(13);
    dispatcher.audio(audio(3030), 13);
    values = collected.wait(12);
    CHECK(values.size() == 12 && values[11]->stream_id != current_stream);
}

static void bounded_media_queue(bool byte_limit) {
    CollectedFrames collected;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    RemoteMediaFrameDispatcher dispatcher(byte_limit ? 256 : 2, byte_limit ? 1920 : 32 * 1024 * 1024);
    dispatcher.listener([&](auto frame) {
        if (frame->audio.render_time_ms == 1000) {
            entered.set_value();
            CHECK(gate.wait_for(5s) == std::future_status::ready);
        }
        collected.add(std::move(frame));
    });
    dispatcher.bind(1);
    dispatcher.audio(audio(1000), 1);
    CHECK(entered.get_future().wait_for(5s) == std::future_status::ready);
    dispatcher.audio(audio(1010), 1);
    dispatcher.audio(audio(1020), 1); // Exceeds the limit including the in-flight first packet.
    release.set_value();
    auto values = collected.wait(2);
    CHECK(values.size() == 2);
    dispatcher.audio(audio(1030), 1);
    values = collected.wait(3);
    CHECK(values[2]->discontinuous);
    CHECK(std::fabs(seconds(values[2]->presentation_time) - 1.03L) < 0.0000001L);
    CHECK(!values[1]->discontinuous);
}

static void listener_replacement_and_reentrancy() {
    CollectedFrames collected;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto life = std::make_shared<int>(1);
    std::weak_ptr<int> weak = life;
    std::atomic<int> old_calls{0};
    RemoteMediaFrameDispatcher dispatcher;
    dispatcher.listener([&, life](auto) {
        ++old_calls;
        entered.set_value();
        CHECK(gate.wait_for(5s) == std::future_status::ready);
    });
    life.reset();
    dispatcher.bind(1);
    dispatcher.audio(audio(), 1);
    CHECK(entered.get_future().wait_for(5s) == std::future_status::ready);
    dispatcher.audio(audio(1010), 1);
    dispatcher.listener([&](auto frame) {
        dispatcher.listener({}); // Removing the listener from its own callback must not deadlock.
        collected.add(std::move(frame));
    });
    CHECK(!weak.expired());
    dispatcher.audio(audio(1020), 1);
    release.set_value();
    const auto values = collected.wait(1);
    CHECK(old_calls == 1 && values[0]->discontinuous);
    CHECK(!dispatcher.has_listener());
    eventually([&] { return weak.expired(); });
}

static void public_options_and_external_sink() {
    xmax::client client("");
    xmax::RealtimeConfiguration settings;
    settings.is_adaptive_quality_enabled = false;
    auto controller = client.create_realtime(settings);
    CHECK(!controller.quality().adaptive_enabled);
    auto original = controller.bind_external({640, 480, 30});
    auto latest = controller.bind_external({640, 480, 30});
    CHECK(!original.requesting() && original.format().width == 640);
    original.push({});
    original.report_error(XMAX_MEDIA_ERROR, "old source");
    CHECK(controller.state_info().reason == XMAX_REASON_NORMAL);
    auto pixels = video(1, 640, 480);
    latest.push(pixels.view());
    CHECK(controller.acquire_local_frame());
    latest.report_error(XMAX_MEDIA_ERROR, "current source", 47, 503);
    const auto state = controller.state_info();
    CHECK(state.event.state == XMAX_IDLE && state.reason == XMAX_REASON_FAILURE);
    CHECK(state.error.api_code == 47 && state.error.http_status == 503);

    const xmax_realtime_options invalid{sizeof(invalid), "x2.1", 2};
    xmax_realtime missing = reinterpret_cast<xmax_realtime>(1);
    CHECK(xmax_realtime_create_with_options(client.get(), &invalid, &missing) == XMAX_INVALID_ARGUMENT);
    CHECK(missing == nullptr);
    const xmax_realtime_config legacy{sizeof(legacy), "x2.1"};
    CHECK(xmax_realtime_create_with_config(client.get(), &legacy, &missing) == XMAX_OK);
    xmax::realtime compatibility(missing);
    CHECK(compatibility.quality().adaptive_enabled);

    xmax::external_frame_sink expired;
    {
        auto temporary = client.create_realtime();
        expired = temporary.bind_external({640, 480, 30});
    }
    CHECK(!expired.requesting());
    expired.push({});
    expired.report_error(XMAX_MEDIA_ERROR, "controller already destroyed");
    CHECK(expired.format().height == 480);
}

static void destruction_with_inflight_source_reference() {
    auto rtc = std::make_shared<FakeRtc>();
    auto* native = new xmax_realtime_impl(std::make_unique<XmaxRealtimeManager>(api(), [rtc] { return rtc; }));
    auto in_flight = native->session;
    xmax_external_frame_sink raw_sink = nullptr;
    const xmax_video_format format{4, 4, 30};
    CHECK(xmax_realtime_bind_external_video(native, &format, &raw_sink) == XMAX_OK);
    xmax::external_frame_sink sink(raw_sink);
    CHECK(xmax_realtime_connect(native, 0) == XMAX_OK && sink.requesting());
    xmax_realtime_destroy(native);
    CHECK(rtc->leaves == 1 && !sink.requesting());
    sink.push({});
    sink.report_error(XMAX_MEDIA_ERROR, "late source failure after handle destruction");
    // The temporary call reference may delay allocation release, but never RTC or worker cleanup.
    in_flight.reset();
    CHECK(!sink.requesting());
}

struct MediaProbe {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<xmax::media_frame> frames;
    std::atomic<int> released{0};
    std::atomic<bool> release_reentry_ok{false};
    void add(xmax::media_frame frame) {
        std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(std::move(frame));
        changed.notify_all();
    }
    void wait(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(changed.wait_for(lock, 5s, [&] { return frames.size() >= count; }));
    }
};

struct MediaContext {
    std::shared_ptr<MediaProbe> probe;
    xmax_realtime controller;
};

static void XMAX_CALL media_callback(void* context, xmax_media_frame borrowed) {
    auto& value = *static_cast<MediaContext*>(context);
    xmax_media_frame_view view{};
    view.struct_size = sizeof(view);
    CHECK(xmax_media_frame_get_view(borrowed, &view) == XMAX_OK);
    xmax_event_info state{};
    state.struct_size = sizeof(state);
    // Application callbacks execute outside the lifecycle and native RTC threads.
    CHECK(xmax_realtime_get_state_info(value.controller, &state) == XMAX_OK);
    xmax_media_frame retained = nullptr;
    CHECK(xmax_media_frame_retain(borrowed, &retained) == XMAX_OK);
    value.probe->add(xmax::media_frame(retained));
}

static void XMAX_CALL release_media_context(void* context) {
    auto* value = static_cast<MediaContext*>(context);
    xmax_event_info state{};
    state.struct_size = sizeof(state);
    value->probe->release_reentry_ok = xmax_realtime_get_state_info(value->controller, &state) == XMAX_OK;
    ++value->probe->released;
    delete value;
}

struct ObservedRtc : FakeRtc {
    bool fail_observation = false;
    void observe_remote_media(bool enabled) override {
        require(!enabled || !fail_observation, XMAX_RTC_ERROR, "Observation unavailable");
        FakeRtc::observe_remote_media(enabled);
    }
};

static void public_media_bridge() {
    auto rtc = std::make_shared<ObservedRtc>();
    auto probe = std::make_shared<MediaProbe>();
    auto native = new xmax_realtime_impl(std::make_unique<XmaxRealtimeManager>(api(), [rtc] { return rtc; }));
    xmax::realtime controller(native);
    auto* context = new MediaContext{probe, native};
    const xmax_remote_media_listener listener{sizeof(listener), media_callback, context, release_media_context};
    CHECK(xmax_realtime_set_remote_media_frame_listener(native, &listener) == XMAX_OK);
    controller.prepare({640, 480, 30});
    controller.connect();
    CHECK(rtc->observing_media && rtc->remote_volume == 0);
    auto pcm = audio();
    rtc->callbacks.audio(pcm);
    pcm.data[0] = 0x7f;
    rtc->callbacks.frame(video());
    probe->wait(2);
    {
        std::lock_guard<std::mutex> lock(probe->mutex);
        const auto first = probe->frames[0].view();
        const auto second = probe->frames[1].view();
        CHECK(first.audio.data[0] == 0x31 && second.video.planes[0][0] == 0x41);
        CHECK(first.stream_id == second.stream_id);
    }

    // Failed registration keeps the old listener and leaves the new context caller-owned.
    rtc->fail_observation = true;
    auto rejected_probe = std::make_shared<MediaProbe>();
    auto* rejected = new MediaContext{rejected_probe, native};
    const xmax_remote_media_listener replacement{sizeof(replacement), media_callback, rejected, release_media_context};
    CHECK(xmax_realtime_set_remote_media_frame_listener(native, &replacement) == XMAX_RTC_ERROR);
    CHECK(rejected_probe->released == 0 && probe->released == 0);
    delete rejected;
    rtc->callbacks.audio(audio(1010));
    probe->wait(3);
    rtc->fail_observation = false;

    const auto previous = rtc->callbacks;
    controller.disconnect();
    controller.connect();
    previous.audio(audio(2000));
    previous.frame(video(2000000));
    rtc->callbacks.audio(audio(2000));
    probe->wait(4);
    {
        std::lock_guard<std::mutex> lock(probe->mutex);
        CHECK(probe->frames.size() == 4);
        CHECK(probe->frames[3].view().stream_id != probe->frames[0].view().stream_id);
        CHECK(probe->frames[3].view().is_discontinuous);
    }

    auto cpp_probe = std::make_shared<MediaProbe>();
    controller.remote_media_listener([cpp_probe](xmax::media_frame frame) {
        cpp_probe->add(std::move(frame));
    });
    eventually([&] { return probe->released == 1; });
    CHECK(probe->release_reentry_ok);
    rtc->callbacks.audio(audio(2010));
    cpp_probe->wait(1);
    {
        std::lock_guard<std::mutex> lock(cpp_probe->mutex);
        auto retained = cpp_probe->frames[0].retain();
        CHECK(retained.view().audio.data[0] == 0x31);
        CHECK(retained.view().is_discontinuous);
    }
    controller.remote_media_listener({});
    CHECK(!rtc->observing_media && rtc->remote_volume == 0);
    controller.close();
    controller = xmax::realtime();
    // Retained packets remain usable after their realtime controller has been destroyed.
    CHECK(probe->frames[0].view().audio.data[0] == 0x31);
}

int main() {
    try {
        disabled_quality();
        generation_replacement();
        replacement_during_connect();
        explicit_disconnect_cancels_request();
        source_preview_revision();
        camera_startup_failure_contract();
        external_source_failures();
        audio_sample_timeline();
        shared_media_timeline();
        bounded_media_queue(false);
        bounded_media_queue(true);
        listener_replacement_and_reentrancy();
        public_options_and_external_sink();
        destruction_with_inflight_source_reference();
        public_media_bridge();
        std::cout << "Realtime request replacement, quality options, external failures and remote media output passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
