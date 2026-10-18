#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
using namespace xmax::detail;
#include "TestSupport.hpp"
static void protocol() {
    const auto id = task_id();
    CHECK(id.size() == 38 && id.substr(0, 5) == "task-" && id.substr(27) == "?os=windows");
    Config c;
    c.key = "key";
    CHECK(c.base_url().find("22duck") != std::string::npos);
    auto http = std::make_shared<FakeHttp>();
    ApiService api(c, http);
    Connection room{"a", "r", "u", "t", "b"};
    auto value = generation_event("start", room, "task", {1920, 1024, 30}, "角色\"\\\n", "https://ref", true);
    CHECK(value["runtime"]["platform"] == "windows");
    CHECK(value["uid"] == value["session_uid"]);
    CHECK(value["params"]["size"] == json::array({1920, 1024}));
    CHECK(value["params"]["face_replace"] == true);
    auto changed = generation_event("change_condition", room, "task", {1280, 720, 30}, "p", "", false);
    CHECK(!changed["params"].contains("mirror"));
    CHECK(!changed["params"].contains("ref_image_path"));
    CHECK(changed["params"]["target_size"] == json::array({1280, 720}));
    CHECK(bitrate({1920, 1080, 30}).second == 3780);
    expect_error(XMAX_INVALID_ARGUMENT,
                 [&] { api.request("GET", "https://evil.invalid", nullptr, [] { return false; }); });
    expect_error(XMAX_INVALID_ARGUMENT,
                 [&] { api.request("GET", "//evil.invalid", nullptr, [] { return false; }); });
    expect_error(XMAX_INVALID_ARGUMENT,
                 [&] { api.request("GET", "/../secret", nullptr, [] { return false; }); });
    CHECK(http->calls.empty());
    CHECK(parse_connection({{"sessionUid", "id"},
                            {"modelExtra", json{{"rtc_app_id", "app"},
                                                {"room_id", "r"},
                                                {"rtc_user_id", "u"},
                                                {"room_token", "t"},
                                                {"rtc_bot_id", "b"}}
                                               .dump()}})
              .room.user == "u");
}
static void chunks() {
    std::string prompt;
    for (int i = 0; i < 1600; ++i)
        prompt += "角色✨\"\\\n";
    const auto source = json{{"prompt", prompt}}.dump();
    auto packets = encode_message(source, "event-1");
    CHECK(packets.size() > 1);
    std::string reconstructed;
    for (size_t i = 0; i < packets.size(); ++i) {
        auto p = json::parse(packets[i]);
        CHECK(p["index"] == i && p["count"] == packets.size());
        CHECK(p["event"] == "__agora_chunk__");
        CHECK(packets[i].size() <= 800);
        reconstructed += p["data"].get<std::string>();
    }
    CHECK(source == reconstructed);
    CHECK(encode_message("{}", "id").size() == 1);
    expect_error(XMAX_INVALID_ARGUMENT, [] { encode_message(std::string(262145, 'a'), "id"); });
}
static void frames() {
    uint8_t pixels[48];
    std::memset(pixels, 42, sizeof(pixels));
    xmax_video_frame s{};
    s.struct_size = sizeof(s);
    s.pixel_format = XMAX_BGRA;
    s.width = 4;
    s.height = 2;
    s.timestamp_us = 0;
    s.planes[0] = pixels;
    s.strides[0] = 24;
    s.plane_sizes[0] = 40;
    auto copy = Frame::copy(s);
    CHECK(copy.planes[0].size() == 32);
    pixels[0] = 0;
    CHECK(copy.planes[0][0] == 42);
    CHECK(copy.strides[0] == 16);
    s.plane_sizes[0] = 39;
    expect_error(XMAX_INVALID_ARGUMENT, [&] { Frame::copy(s); });
    s.plane_sizes[0] = 40;
    s.strides[0] = SIZE_MAX;
    expect_error(XMAX_INVALID_ARGUMENT, [&] { Frame::copy(s); });
    s.strides[0] = 24;
    s.timestamp_us = -1;
    expect_error(XMAX_INVALID_ARGUMENT, [&] { Frame::copy(s); });
}
static void lifecycle() {
    auto h = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    Config c;
    c.key = "test";
    XmaxRealtimeManager s(std::make_shared<ApiService>(c, h), [rtc] { return rtc; });
    s.prepare({1920, 1024, 30});
    s.start("hello", "ref", true, false);
    CHECK(rtc->joins == 1 && s.requesting());
    s.update("updated", "", false);
    CHECK(rtc->messages_count() >= 2);
    s.volume(0.5f);
    expect_error(XMAX_INVALID_ARGUMENT, [&] { s.volume(NAN); });
    Frame frame;
    frame.width = 4;
    frame.height = 4;
    frame.format = XMAX_BGRA;
    frame.strides[0] = 16;
    frame.planes[0].resize(64);
    frame.timestamp_us = 1;
    rtc->callbacks.frame(frame);
    CHECK(s.acquire_frame() != nullptr);
    CHECK(s.acquire_frame() == nullptr);
    const auto old_frame_callback = rtc->callbacks.frame;
    s.disconnect(false);
    CHECK(!s.requesting());
    CHECK(h->count("DELETE /session/session-1") == 1);
    CHECK(rtc->leaves == 1);
    rtc->callbacks.frame(frame);
    CHECK(!s.acquire_frame());
    s.start("again", "", false, false);
    old_frame_callback(frame);
    CHECK(!s.acquire_frame());
    s.disconnect(true);
    s.disconnect(true);
    CHECK(h->count("DELETE /session/session-1") == 2);
    expect_error(XMAX_INVALID_STATE, [&] { s.start("bad", "", false, false); });
}
static void malformed_and_unavailable() {
    auto h = std::make_shared<FakeHttp>();
    h->malformed = true;
    auto rtc = std::make_shared<FakeRtc>();
    Config c;
    c.key = "test";
    XmaxRealtimeManager s(std::make_shared<ApiService>(c, h), [rtc] { return rtc; });
    s.prepare({1920, 1024, 30});
    expect_error(XMAX_API_ERROR, [&] { s.start("p", "", false, false); });
    CHECK(h->count("DELETE /session/session-1") == 1);
    CHECK(rtc->joins == 0);
    auto unused = std::make_shared<FakeHttp>();
    XmaxRealtimeManager missing(std::make_shared<ApiService>(c, unused), [] {
        throw Error(XMAX_UNSUPPORTED, "No RTC");
        return std::shared_ptr<Rtc>{};
    });
    missing.prepare({1920, 1024, 30});
    expect_error(XMAX_UNSUPPORTED, [&] { missing.start("p", "", false, false); });
    CHECK(unused->calls.empty());
}
static void cancellation() {
    auto h = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    rtc->block_join = true;
    Config c;
    c.key = "test";
    XmaxRealtimeManager s(std::make_shared<ApiService>(c, h), [rtc] { return rtc; });
    s.prepare({1920, 1024, 30});
    auto start = std::async(std::launch::async,
                            [&] { expect_error(XMAX_CANCELLED, [&] { s.start("p", "", false, false); }); });
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!rtc->joined && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(rtc->joined);
    s.disconnect(true);
    start.get();
    CHECK(!s.requesting());
    CHECK(h->count("DELETE /session/session-1") == 1);
}
static void heartbeat() {
    auto h = std::make_shared<FakeHttp>();
    h->heartbeat_failure = true;
    auto rtc = std::make_shared<FakeRtc>();
    Config c;
    c.key = "test";
    XmaxRealtimeManager s(
        std::make_shared<ApiService>(c, h), [rtc] { return rtc; }, std::chrono::milliseconds(10));
    s.prepare({1920, 1024, 30});
    s.start("p", "", false, false);
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    bool error = false;
    while (Clock::now() < deadline && !error) {
        xmax_event e{};
        while (s.poll(e))
            if (e.type == XMAX_EVENT_ERROR)
                error = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(error);
    CHECK(!s.requesting());
    CHECK(h->count("PUT /session/session-1/heartbeat") == 1);
    CHECK(h->count("DELETE /session/session-1") == 1);
}
static void startup_heartbeat_and_close_failure() {
    auto h = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    rtc->remote_delay_ms = 40;
    Config c;
    c.key = "test";
    XmaxRealtimeManager s(
        std::make_shared<ApiService>(c, h), [rtc] { return rtc; }, std::chrono::milliseconds(5));
    s.prepare({1920, 1024, 30});
    s.start("p", "", false, false);
    CHECK(h->count("PUT /session/session-1/heartbeat") > 0);
    h->close_failure = true;
    expect_error(XMAX_API_ERROR, [&] { s.disconnect(true); });
    CHECK(!s.requesting());
    bool reported = false;
    xmax_event event{};
    while (s.poll(event))
        if (event.type == XMAX_EVENT_ERROR && std::string(event.session_id) == "session-1")
            reported = true;
    CHECK(reported);
    expect_error(XMAX_API_ERROR, [] { parse_connection({{"sessionUid", "s"}, {"modelExtra", "{broken"}}); });
}
int main() {
    int failures = 0;
    for (auto test : std::vector<std::pair<const char*, void (*)()>>{
             {"protocol", protocol},
             {"UTF-8 chunks", chunks},
             {"frame ownership", frames},
             {"lifecycle", lifecycle},
             {"failure rollback", malformed_and_unavailable},
             {"cancel during join", cancellation},
             {"heartbeat termination", heartbeat},
             {"startup heartbeat and close failure", startup_heartbeat_and_close_failure}}) {
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
