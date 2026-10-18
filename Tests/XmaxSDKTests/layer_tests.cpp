#include "Media/MediaController.hpp"
#include "Render/RenderController.hpp"
#include "Service/Realtime/RealtimeSessionService.hpp"
#include "Stream/StreamController.hpp"
#include "TestSupport.hpp"
#include <future>
#include <iostream>

struct ByteHttp : Http {
    HttpResponse response{200, R"({"success":true,"data":{"value":42}})"};
    HttpRequest received;
    int calls = 0;
    HttpResponse request(const HttpRequest& request, const Cancel&) override {
        received = request;
        ++calls;
        return response;
    }
};
static void api_transport_boundary() {
    auto http = std::make_shared<ByteHttp>();
    Config config;
    config.key = "test-key";
    config.model = "x2.1.1-preview-us";
    config.timeout_ms = 2345;
    ApiService api(config, http);
    const json body{{"prompt", "你好✨"}};
    CHECK(api.request("POST", "/media", &body, [] { return false; })["value"] == 42);
    CHECK(http->received.url == "https://api.xmax.ai/open/api/v1/media");
    CHECK(http->received.method == "POST" && http->received.timeout_ms == 2345);
    CHECK(json::parse(http->received.body) == body);
    CHECK(std::find(http->received.headers.begin(), http->received.headers.end(),
                    std::make_pair(std::string("X-Api-Key"), std::string("test-key"))) !=
          http->received.headers.end());
    for (const auto& response : std::vector<HttpResponse>{{503, "unavailable"},
                                                          {200, "not JSON"},
                                                          {200, R"({"success":false,"data":{}})"},
                                                          {200, R"({"success":"true","data":{}})"},
                                                          {200, R"({"success":true})"}}) {
        http->response = response;
        expect_error(XMAX_API_ERROR, [&] { api.request("GET", "/media", nullptr, [] { return false; }); });
    }
    const auto count = http->calls;
    expect_error(XMAX_CANCELLED, [&] { api.request("GET", "/media", nullptr, [] { return true; }); });
    CHECK(http->calls == count);
}
static void service_rollback_ownership() {
    auto http = std::make_shared<FakeHttp>();
    Config config;
    config.key = "test";
    RealtimeSessionService service(std::make_shared<ApiService>(config, http));
    http->malformed = true;
    expect_error(XMAX_API_ERROR, [&] { service.create([] { return false; }); });
    CHECK(service.active().id == "session-1");
    service.close();
    service.close();
    CHECK(http->count("DELETE /session/session-1") == 1);
    http->malformed = false;
    service.create([] { return false; });
    http->close_failure = true;
    expect_error(XMAX_NETWORK_ERROR, [&] { service.close(); });
    CHECK(service.active().id.empty());
}
static void stream_without_core() {
    auto rtc = std::make_shared<FakeRtc>();
    StreamController stream([rtc] { return rtc; });
    stream.volume(0.4f);
    stream.allocate();
    const auto cancel = [] { return false; };
    stream.join({"app", "room", "user", "token", "bot"}, {1920, 1080, 30}, true, {}, cancel);
    CHECK(rtc->encoding.format.width == 1536 && rtc->encoding.format.height == 864 && rtc->encoding.format.fps == 24);
    CHECK(rtc->encoding.minimum_bitrate == 100);
    CHECK(rtc->microphone && rtc->remote_volume == 0.4f);
    stream.start("角色✨", "https://ref", true, cancel);
    const auto task = stream.task();
    stream.update("updated", "", false, cancel);
    stream.heartbeat(cancel);
    stream.push(Frame{});
    CHECK(rtc->pushes == 1);
    stream.stop();
    stream.push(Frame{});
    stream.stop();
    CHECK(rtc->pushes == 1 && rtc->leaves == 1);
    CHECK(rtc->messages_count() == 4);
    CHECK(json::parse(rtc->messages[0])["event"] == "start");
    CHECK(json::parse(rtc->messages[1])["uid"] == task);
    CHECK(json::parse(rtc->messages[2])["event"] == "heartbeat");
    CHECK(json::parse(rtc->messages[3])["event"] == "stop");
    CHECK(stream.task().empty());
}
static Frame small_frame(int64_t timestamp) {
    Frame frame;
    frame.width = frame.height = 4;
    frame.timestamp_us = timestamp;
    frame.strides[0] = 16;
    frame.planes[0].assign(64, 42);
    return frame;
}
static void media_without_rtc() {
    MediaController media;
    auto& input = media.external();
    input.prepare({4, 4, 30});
    auto source = small_frame(0);
    input.push(source.view());
    CHECK(input.bound() && !input.requesting());
    Frame received;
    int deliveries = 0;
    input.start([&](const Frame& frame) {
        received = frame;
        ++deliveries;
    });
    source.timestamp_us = 1;
    input.push(source.view());
    source.planes[0][0] = 0;
    CHECK(received.planes[0][0] == 42 && deliveries == 1);
    expect_error(XMAX_INVALID_ARGUMENT, [&] { input.push(source.view()); });
    media.stop();
    CHECK(input.bound() && !input.requesting());
    source.timestamp_us = 2;
    input.push(source.view());
    CHECK(deliveries == 1);
    media.close();
    CHECK(!input.bound());
    expect_error(XMAX_INVALID_STATE, [&] { input.push(source.view()); });
}
static void media_stop_drains_delivery() {
    ExternalVideoController input;
    input.prepare({4, 4, 30});
    std::promise<void> entered, release;
    auto release_future = release.get_future().share();
    input.start([&](const Frame&) {
        entered.set_value();
        release_future.wait();
    });
    auto push = std::async(std::launch::async, [&] { input.push(small_frame(0).view()); });
    entered.get_future().wait();
    auto stop = std::async(std::launch::async, [&] { input.stop(); });
    const auto blocked = stop.wait_for(std::chrono::milliseconds(20));
    release.set_value();
    push.get();
    stop.get();
    CHECK(blocked == std::future_status::timeout);
    CHECK(!input.requesting());
}
static void render_rejects_stale_streams() {
    RenderController render;
    render.bind(10);
    render.receive(small_frame(1), 10);
    render.receive(small_frame(2), 10);
    auto held = render.acquire_frame();
    CHECK(held && held->timestamp_us == 2);
    CHECK(!render.acquire_frame());
    render.unbind(11);
    render.bind(10); // A late join from a cancelled generation cannot reactivate rendering.
    render.receive(small_frame(3), 10);
    CHECK(!render.acquire_frame());
    render.bind(11);
    render.receive(small_frame(4), 11);
    render.receive(small_frame(999), 10); // Old callbacks cannot overwrite the new generation.
    CHECK(render.acquire_frame()->timestamp_us == 4);
    render.unbind(12);
    CHECK(held->timestamp_us == 2 && held->planes[0][0] == 42);
}
int main() {
    int failures = 0;
    for (auto test : std::vector<std::pair<const char*, void (*)()>>{
             {"API and byte transport boundary", api_transport_boundary},
             {"server allocation ownership", service_rollback_ownership},
             {"stream without Core", stream_without_core},
             {"media without RTC", media_without_rtc},
             {"media delivery draining", media_stop_drains_delivery},
             {"render generation isolation", render_rejects_stale_streams}}) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
