#pragma once
#include "Foundation/Json.hpp"
#include "Foundation/Network/HttpTransport.hpp"
#include "Foundation/RTC/RtcPlatform.hpp"
#include <algorithm>
#include <mutex>
#include <thread>
using namespace xmax::detail;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("Line ") + std::to_string(__LINE__) + ": " + #x);           \
    } while (0)
template <class F> void expect_error(xmax_result code, F f) {
    try {
        f();
        throw std::runtime_error("Expected SDK error");
    } catch (const Error& e) {
        CHECK(e.code == code);
    }
}
struct FakeHttp : Http {
    std::mutex mutex;
    std::vector<std::string> calls;
    std::atomic<bool> malformed{false}, heartbeat_failure{false}, close_failure{false};
    HttpResponse request(const HttpRequest& input, const Cancel&) override {
        const auto& method = input.method;
        const auto path = input.url.substr(input.url.find("/open/api/v1") + 12);
        const auto response = [](json data) -> HttpResponse {
            return {200, json{{"success", true}, {"data", std::move(data)}}.dump()};
        };
        std::lock_guard<std::mutex> lock(mutex);
        calls.push_back(method + " " + path);
        if (method == "POST") {
            json value{{"sessionUid", "session-1"},
                       {"modelExtra",
                        {{"rtc_app_id", "app"},
                         {"room_id", "room"},
                         {"rtc_user_id", "user"},
                         {"room_token", "token"},
                         {"rtc_bot_id", "bot"}}}};
            if (malformed)
                value["modelExtra"].erase("room_token");
            return response(value);
        }
        if (method == "PUT")
            return response({{"status", heartbeat_failure ? "CLOSED" : "ACTIVE"}});
        if (method == "DELETE" && close_failure)
            throw Error(XMAX_NETWORK_ERROR, "Close unavailable");
        return response(json::object());
    }
    int count(const std::string& call) {
        std::lock_guard<std::mutex> lock(mutex);
        return int(std::count(calls.begin(), calls.end(), call));
    }
};
struct FakeRtc : Rtc {
    std::atomic<int> joins{0}, leaves{0}, pushes{0};
    std::atomic<bool> joined{false}, block_join{false};
    int remote_delay_ms = 0;
    std::mutex mutex;
    std::vector<std::string> messages;
    RtcCallbacks callbacks;
    VideoEncodingConfiguration encoding{};
    float remote_volume = -1;
    bool microphone = false;
    bool observing_media = false;
    void join(const Connection&, VideoEncodingConfiguration config, bool mic, RtcCallbacks cb,
              const Cancel& cancel) override {
        callbacks = std::move(cb);
        encoding = config;
        microphone = mic;
        ++joins;
        joined = true;
        while (block_join && !cancel())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(!cancel(), XMAX_CANCELLED, "Join cancelled");
    }
    void send(const std::string& s) override {
        std::lock_guard<std::mutex> lock(mutex);
        messages.push_back(s);
    }
    void push(const Frame&) override { ++pushes; }
    void begin_generation() override {}
    void wait_remote(const Cancel& cancel, const std::function<void()>& keepalive) override {
        const auto until = Clock::now() + std::chrono::milliseconds(remote_delay_ms);
        while (Clock::now() < until && !cancel()) {
            keepalive();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        keepalive();
        require(!cancel(), XMAX_CANCELLED, "Cancelled");
    }
    void volume(float value) override { remote_volume = value; }
    void observe_remote_media(bool enabled) override { observing_media = enabled; }
    void configure_encoding(VideoEncodingConfiguration value) override { encoding = value; }
    void leave() noexcept override { ++leaves; }
    int messages_count() {
        std::lock_guard<std::mutex> lock(mutex);
        return int(messages.size());
    }
};
