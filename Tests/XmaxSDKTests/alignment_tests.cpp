#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Core/XmaxClient.hpp"
#include "Service/Media/MediaService.hpp"
#include "TestSupport.hpp"
#include <xmax/xmax.hpp>
#include <cmath>
#include <iostream>

struct RecordingHttp : FakeHttp {
    std::vector<HttpRequest> requests;
    HttpResponse request(const HttpRequest& input, const Cancel& cancel) override {
        requests.push_back(input);
        return FakeHttp::request(input, cancel);
    }
};

static void model_and_environment() {
    const auto model = RealtimeModelDefinition::info(RealtimeModelDefinition::parse("x2.1"));
    CHECK(model.format_count == 5 && model.default_camera_format.width == 1080);
    CHECK(model.default_camera_format.height == 1920 && model.maximum_input_pixels == 2100000);
    for (int value : {1, 2}) {
        const auto removed = static_cast<xmax::RealtimeModel>(value);
        CHECK(xmax::model_identifier(removed)[0] == 0);
        expect_error(XMAX_INVALID_ARGUMENT, [&] { RealtimeModelDefinition::info(removed); });
    }
    Config config;
    config.model = "x2.1";
    config.environment = XMAX_ENVIRONMENT_GLOBAL;
    CHECK(config.base_url() == "https://api.xmax.cloud/open/api/v1");
    CHECK(config.base_url(true) == "https://api.xmax.ai/open/api/v1");
    config.key = "test";
    auto http = std::make_shared<RecordingHttp>();
    auto api = std::make_shared<ApiService>(config, http);
    api->request("GET", "/media", nullptr, [] { return false; });
    RealtimeSessionService session(api);
    session.create([] { return false; });
    CHECK(http->requests.front().url.find("api.xmax.cloud") != std::string::npos);
    CHECK(http->requests.back().url.find("api.xmax.ai") != std::string::npos);
    CHECK(json::parse(http->requests.back().body)["model"] == "x2.1");
    session.close();
    expect_error(XMAX_INVALID_ARGUMENT, [] { RealtimeModelDefinition::parse("unknown"); });
    CHECK(MediaService::input({1280, 720, 30}).width == 1280);
    expect_error(XMAX_INVALID_ARGUMENT, [] { MediaService::camera_input({1280, 720, 30}); });
}

struct FailingHttp : Http {
    HttpResponse request(const HttpRequest&, const Cancel&) override {
        return {403, R"({"success":false,"code":1234,"message":"access rejected"})"};
    }
};

static void error_details() {
    Config config;
    config.key = "test";
    auto api = std::make_shared<ApiService>(config, std::make_shared<FailingHttp>());
    XmaxRealtimeManager manager(api, [] { return std::make_shared<FakeRtc>(); });
    manager.prepare({1280, 720, 30});
    try {
        manager.connect(false);
        CHECK(false);
    } catch (const Error& error) {
        CHECK(error.code == XMAX_API_ERROR && error.api_code == 1234 && error.http_status == 403);
    }
    const auto state = manager.state_info();
    CHECK(state.event.state == XMAX_READY && state.reason == XMAX_REASON_FAILURE);
    CHECK(state.error.api_code == 1234 && state.error.http_status == 403);
    xmax_event_info event{};
    bool reason_found = false;
    while (manager.poll(event))
        reason_found = reason_found || (event.reason == XMAX_REASON_FAILURE && event.error.http_status == 403);
    CHECK(reason_found);
    manager.disconnect_reason(XMAX_REASON_ORIENTATION_CHANGED);
    CHECK(manager.state_info().reason == XMAX_REASON_ORIENTATION_CHANGED);
    manager.prepare({1280, 720, 30});
    CHECK(manager.state_info().reason == XMAX_REASON_NORMAL);
}

struct CameraProbe {
    int starts = 0;
    xmax_video_format current{};
    uint32_t fail_width = 0;
};
struct ProbeCamera : CameraCapture {
    std::shared_ptr<CameraProbe> probe;
    explicit ProbeCamera(std::shared_ptr<CameraProbe> value) : probe(std::move(value)) {}
    std::vector<CameraDevice> devices() override { return {{"one", "one", true}}; }
    void start(const std::string&, xmax_video_format format, CaptureCallbacks callbacks,
               const Cancel&) override {
        ++probe->starts;
        probe->current = format;
        require(format.width != probe->fail_width, XMAX_MEDIA_ERROR, "Device configuration failed");
        Frame frame;
        frame.width = 640;
        frame.height = 480;
        frame.timestamp_us = 1;
        frame.strides[0] = 2560;
        frame.planes[0].resize(2560 * 480);
        callbacks.frame(std::move(frame));
    }
    void stop() noexcept override {}
};

static void camera_configuration() {
    auto probe = std::make_shared<CameraProbe>();
    Config config;
    XmaxRealtimeManager manager(std::make_shared<ApiService>(config), [] { return std::make_shared<FakeRtc>(); },
        std::chrono::seconds(10), [probe] { return std::make_unique<ProbeCamera>(probe); });
    CHECK(manager.camera_format().width == 0);
    manager.prepare_camera("one", {1920, 1080, 24}, false);
    manager.update_camera_format({1080, 1920, 24});
    CHECK(manager.camera_format().height == 1920 && manager.camera_format().fps == 24);
    CHECK(manager.acquire_local_frame()->width == 640);
    probe->fail_width = 1408;
    expect_error(XMAX_MEDIA_ERROR, [&] { manager.update_camera_format({1408, 1408, 24}); });
    CHECK(manager.state_snapshot().state == XMAX_READY);
    CHECK(manager.camera_format().width == 1080 && probe->current.width == 1080);
    CHECK(manager.acquire_local_frame());
    manager.stop_media();
    CHECK(manager.camera_format().width == 0);
}

struct ControlledRtc : FakeRtc {
    bool fail_encoding = false, fail_send = false;
    std::atomic<bool> block_send{false}, sending{false};
    void configure_encoding(VideoEncodingConfiguration value) override {
        require(!fail_encoding, XMAX_RTC_ERROR, "Encoder failed");
        FakeRtc::configure_encoding(value);
    }
    void send(const std::string& value) override {
        require(!fail_send, XMAX_RTC_ERROR, "Signal failed");
        sending = true;
        while (block_send)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        FakeRtc::send(value);
    }
};

static void manual_quality_and_interaction() {
    auto rtc = std::make_shared<ControlledRtc>();
    auto http = std::make_shared<FakeHttp>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager manager(std::make_shared<ApiService>(config, http), [rtc] { return rtc; });
    CHECK(manager.quality().uplink_level == 0);
    manager.prepare({1280, 720, 30});
    expect_error(XMAX_INVALID_STATE, [&] { manager.quality_level(0, 1); });
    manager.start("prompt", "", false, false);
    CHECK(rtc->encoding.minimum_bitrate == 500);
    manager.quality_level(0, 1);
    CHECK(manager.quality().uplink_level == 1 && rtc->encoding.format.width == 1280);
    rtc->fail_encoding = true;
    expect_error(XMAX_RTC_ERROR, [&] { manager.quality_level(0, 4); });
    CHECK(manager.quality().uplink_level == 1);
    rtc->fail_encoding = false;
    manager.quality_level(1, 4);
    CHECK(manager.quality().downlink_level == 4);
    rtc->fail_send = true;
    expect_error(XMAX_RTC_ERROR, [&] { manager.quality_level(1, 3); });
    CHECK(manager.quality().downlink_level == 4);
    rtc->fail_send = false;
    manager.encoding({sizeof(xmax_encoding_config), -1, 80, XMAX_ENCODER_AUTO});
    CHECK(rtc->encoding.minimum_bitrate == 80 && rtc->encoding.maximum_bitrate == 80);
    xmax_point points[]{{50, 50}, {10, 10}, {75, 75}};
    xmax_interaction_frame sample{sizeof(sample), points, 3, 100, 100, 1920, 1080, XMAX_CONTENT_FIT};
    const auto mapped = InteractionCoordinateMapper::map(sample);
    CHECK(mapped.size() == 2 && std::abs(mapped[0].x - .5) < 1e-9 && std::abs(mapped[0].y - .5) < 1e-9);
    manager.submit_interaction(sample);
    manager.state_snapshot(); // Barrier after the queued trajectory sender.
    bool tracks_found = false;
    for (const auto& encoded : rtc->messages) {
        const auto message = json::parse(encoded);
        if (message["event"] == "tracks") {
            tracks_found = true;
            CHECK(message["uid"] == manager.state_snapshot().task_id);
            CHECK(message["tracks"].size() == 2);
            CHECK(message["tracks"][0] == json::array({.5, .5}));
        }
    }
    CHECK(tracks_found && manager.trajectory().size() == 2);
    manager.disconnect(false);
    CHECK(manager.trajectory().empty() && manager.quality().uplink_level == 0);
    expect_error(XMAX_INVALID_STATE, [&] { manager.submit_interaction(sample); });
    sample.points = nullptr;
    expect_error(XMAX_INVALID_ARGUMENT, [&] { InteractionCoordinateMapper::map(sample); });
}

static void interaction_does_not_block_ui() {
    auto rtc = std::make_shared<ControlledRtc>();
    Config config;
    config.key = "test";
    XmaxRealtimeManager manager(std::make_shared<ApiService>(config, std::make_shared<FakeHttp>()),
                                [rtc] { return rtc; });
    manager.prepare({1280, 720, 30});
    manager.start("prompt", "", false, false);
    rtc->sending = false;
    rtc->block_send = true;
    xmax_point point{50, 50};
    xmax_interaction_frame frame{sizeof(frame), &point, 1, 100, 100, 1280, 720, XMAX_CONTENT_FILL};
    manager.submit_interaction(frame);
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!rtc->sending && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool sender_started = rtc->sending;
    auto submit = std::async(std::launch::async, [&] {
        for (int i = 0; i < 100; ++i) {
            point.x = double(i);
            manager.submit_interaction(frame);
        }
    });
    const bool ui_returned = submit.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    rtc->block_send = false;
    submit.get();
    manager.state_snapshot();
    manager.state_snapshot();
    CHECK(sender_started && ui_returned);
    int count = 0;
    json last;
    {
        std::lock_guard<std::mutex> lock(rtc->mutex);
        for (const auto& text : rtc->messages) {
            auto message = json::parse(text);
            if (message["event"] == "tracks") {
                ++count;
                last = message;
            }
        }
    }
    CHECK(count == 2);
    CHECK(last["tracks"][0][0].get<double>() > .7);
}

static void room_owns_partial_join_cleanup() {
    struct JoinFailure : FakeRtc {
        bool fail = true;
        void join(const Connection& connection, VideoEncodingConfiguration encoding, bool microphone,
                  RtcCallbacks callbacks, const Cancel& cancel) override {
            FakeRtc::join(connection, encoding, microphone, std::move(callbacks), cancel);
            require(!fail, XMAX_RTC_ERROR, "Join failed after backend allocation");
        }
    };
    auto rtc = std::make_shared<JoinFailure>();
    RoomController room([rtc] { return rtc; });
    Connection connection;
    connection.room = "room";
    VideoEncodingConfiguration encoding{{1280, 720, 30}, 500, 2000, XMAX_ENCODER_AUTO};
    room.allocate();
    expect_error(XMAX_RTC_ERROR, [&] { room.join(connection, encoding, false, {}, [] { return false; }); });
    CHECK(!room.joined() && !room.backend() && rtc->leaves == 1);
    rtc->fail = false;
    room.allocate();
    room.join(connection, encoding, false, {}, [] { return false; });
    CHECK(room.joined() && room.backend());
    room.leave();
    room.leave();
    CHECK(!room.joined() && rtc->leaves == 2);
}

static void independent_timing_and_error_ownership() {
    auto now = Clock::time_point(std::chrono::seconds(1));
    RealtimeTiming timing([&] { return now; });
    timing.begin();
    CHECK(timing.snapshot().first_frame_ms == -1);
    now += std::chrono::milliseconds(120);
    timing.finish_session();
    timing.begin_join();
    now += std::chrono::milliseconds(70);
    timing.begin_publication();
    auto publication = timing.publication();
    CHECK(RealtimeTiming::published(publication, now + std::chrono::milliseconds(12)));
    CHECK(!RealtimeTiming::published(publication, now + std::chrono::milliseconds(20)));
    now += std::chrono::milliseconds(30);
    timing.finish_join();
    timing.begin_signal();
    now += std::chrono::milliseconds(250);
    timing.finish_signal();
    const auto snapshot = timing.snapshot();
    CHECK(snapshot.session_ms == 120 && snapshot.rtc_join_ms == 100);
    CHECK(snapshot.publish_ms == 12 && snapshot.first_frame_ms == 250 && snapshot.total_ms == 470);
    timing.reset_publication();
    timing.begin();
    timing.begin_join();
    CHECK(!timing.update_publication(publication));
    CHECK(timing.snapshot().publish_ms == -1);

    RealtimeErrorHandler errors;
    const auto secret = XmaxLogger::protect_secret("STRUCTURE_SECRET_SENTINEL");
    errors.record(Error(XMAX_API_ERROR, "STRUCTURE_SECRET_SENTINEL: 拒绝访问", 123, 403));
    const auto details = errors.details();
    CHECK(details.code == XMAX_API_ERROR && details.api_code == 123 && details.http_status == 403);
    CHECK(std::string(details.message).find(*secret) == std::string::npos);
    CHECK(errors.reason() == XMAX_REASON_FAILURE && errors.error());
    errors.reset(XMAX_REASON_ORIENTATION_CHANGED);
    CHECK(!errors.error() && errors.details().code == XMAX_OK);
    CHECK(errors.reason() == XMAX_REASON_ORIENTATION_CHANGED);
}

static void public_configuration() {
    xmax_model_info info{};
    info.struct_size = sizeof(info);
    CHECK(xmax_model_get_info("x2.1", &info) == XMAX_OK && info.default_camera_format.width == 1080);
    CHECK(xmax_model_get_info("unknown", &info) == XMAX_INVALID_ARGUMENT);
    xmax_error_info error{};
    error.struct_size = sizeof(error);
    CHECK(xmax_last_error_info(&error) == XMAX_OK && error.code == XMAX_INVALID_ARGUMENT);
    CHECK(xmax_last_error_info(&error) == XMAX_OK && error.code == XMAX_INVALID_ARGUMENT);
    xmax::client client(xmax::XmaxConfiguration{"", xmax::XmaxEnvironment::global});
    auto realtime = client.create_realtime(xmax::RealtimeConfiguration{xmax::RealtimeModel::x2_1});
    CHECK(realtime.camera_preset() == xmax::RealtimeCameraPreset::none);
    realtime.prepare({1280, 720, 30});
    const auto state = realtime.state_info();
    CHECK(state.event.state == XMAX_READY && state.reason == XMAX_REASON_NORMAL);
}

int main() {
    try {
        model_and_environment();
        error_details();
        camera_configuration();
        manual_quality_and_interaction();
        public_configuration();
        independent_timing_and_error_ownership();
        room_owns_partial_join_cleanup();
        interaction_does_not_block_ui();
        std::cout << "Alignment contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
