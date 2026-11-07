//
//  billing_heartbeat_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "TestSupport.hpp"
#include <iostream>
using namespace std::chrono_literals;

template <class F> static void eventually(F condition) {
    const auto until = Clock::now() + 5s;
    while (!condition()) {
        CHECK(Clock::now() < until);
        std::this_thread::sleep_for(1ms);
    }
}

struct ReleaseFlag {
    std::atomic<bool>& flag;

    ~ReleaseFlag() {
        flag = true;
    }
};

static std::shared_ptr<ApiService> api(const std::shared_ptr<Http>& http) {
    Config config;
    config.key = "test";
    return std::make_shared<ApiService>(config, http);
}

static std::vector<HttpRequest> heartbeat_requests(FakeHttp& http) {
    std::lock_guard<std::mutex> lock(http.mutex);
    std::vector<HttpRequest> requests;
    for (const auto& request : http.requests)
        if (request.method == "PUT")
            requests.push_back(request);
    return requests;
}

static void first_request_grace_and_periodic_body() {
    auto http = std::make_shared<FakeHttp>();
    http->model_id = true;
    RealtimeSessionService service(api(http));
    const auto session = service.create([] { return false; });
    CHECK(session.model_id == "model-1");
    std::atomic<int> failures{0};
    service.start_heartbeat(9, 5ms, [&](auto) { ++failures; });
    eventually([&] { return heartbeat_requests(*http).size() >= 3; });
    service.stop_heartbeat();
    const auto requests = heartbeat_requests(*http);
    CHECK(json::parse(requests[0].body) == json({{"firstHeartbeatGraceSeconds", 9}}));
    for (size_t i = 1; i < requests.size(); ++i)
        CHECK(requests[i].body.empty());
    CHECK(failures == 0);
    service.close();
}

static void malformed_heartbeat_requires_session_identity() {
    struct MissingSession : FakeHttp {
        HttpResponse request(const HttpRequest&, const Cancel&) override {
            return {200, R"({"success":true,"data":{"status":"ACTIVE"}})"};
        }
    };
    auto service = api(std::make_shared<MissingSession>());
    expect_error(XMAX_SESSION_ERROR, [&] {
        RealtimeSessionHeartbeat::request(*service, "session-1", [] { return false; }, 0);
    });
}

struct ReadyRtc : FakeRtc {
    std::atomic<bool> waiting{false}, result{false};
    std::atomic<bool> fail{false};

    void wait_remote(const Cancel& cancelled, Clock::time_point) override {
        waiting = true;
        while (!result && !cancelled()) {
            std::this_thread::sleep_for(1ms);
        }
        require(!cancelled(), XMAX_CANCELLED, "Generation cancelled");
        if (fail)
            throw Error(XMAX_RTC_ERROR, "No first result frame");
        display_result();
    }
};

static void starts_after_generating_once_per_session() {
    auto http = std::make_shared<FakeHttp>();
    http->model_id = true;
    auto rtc = std::make_shared<ReadyRtc>();
    XmaxRealtimeManager sdk(api(http), [rtc] { return rtc; }, 5ms);
    sdk.prepare({1280, 720, 30});
    sdk.connect(false);
    std::this_thread::sleep_for(20ms);
    CHECK(http->count("PUT /session/session-1/heartbeat") == 0);
    auto start = std::async(std::launch::async, [&] { sdk.start("prompt", "", false, false); });
    ReleaseFlag release_result{rtc->result};
    eventually([&] { return rtc->waiting.load(); });
    std::this_thread::sleep_for(20ms);
    CHECK(http->count("PUT /session/session-1/heartbeat") == 0);
    rtc->result = true;
    start.get();
    eventually([&] { return heartbeat_requests(*http).size() >= 2; });
    CHECK(sdk.state_snapshot().state == XMAX_GENERATING);
    const auto initial = heartbeat_requests(*http);
    CHECK(json::parse(initial[0].body)["firstHeartbeatGraceSeconds"].get<int>() >= 1);
    sdk.update("updated", "", false);
    eventually([&] { return http->count("POST /client-event/relay") >= 2; });
    auto requests = heartbeat_requests(*http);
    size_t firsts = 0;
    for (const auto& request : requests)
        if (!request.body.empty())
            ++firsts;
    CHECK(firsts == 1);
    sdk.disconnect(false);
    const auto count = heartbeat_requests(*http).size();
    std::this_thread::sleep_for(20ms);
    CHECK(heartbeat_requests(*http).size() == count);
    sdk.connect(false);
    CHECK(heartbeat_requests(*http).size() == count);
    sdk.start("new connection", "", false, false);
    eventually([&] { return heartbeat_requests(*http).size() > count; });
    requests = heartbeat_requests(*http);
    CHECK(json::parse(requests[count].body).contains("firstHeartbeatGraceSeconds"));
    sdk.disconnect(true);
}

static void failed_start_has_no_billing_heartbeat() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<ReadyRtc>();
    rtc->result = true;
    rtc->fail = true;
    XmaxRealtimeManager sdk(api(http), [rtc] { return rtc; }, 5ms);
    sdk.prepare({1280, 720, 30});
    expect_error(XMAX_RTC_ERROR, [&] { sdk.start("prompt", "", false, false); });
    CHECK(heartbeat_requests(*http).empty());
    CHECK(http->count("DELETE /session/session-1") == 1);
}

struct ClosedHttp : FakeHttp {
    HttpResponse request(const HttpRequest& input, const Cancel& cancel) override {
        if (input.method != "PUT")
            return FakeHttp::request(input, cancel);
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back("PUT /session/session-1/heartbeat");
            requests.push_back(input);
        }
        return {200, json{{"success", true},
                          {"data", {{"sessionUid", "session-1"}, {"status", "CLOSED"}, {"closeReason", "Credits exhausted"}}}}
                         .dump()};
    }
};

static void closure_reason_preserved() {
    auto http = std::make_shared<ClosedHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    XmaxRealtimeManager sdk(api(http), [rtc] { return rtc; }, 5ms);
    sdk.prepare({1280, 720, 30});
    sdk.start("prompt", "", false, false);
    eventually([&] { return http->count("DELETE /session/session-1") == 1; });
    CHECK(sdk.state_snapshot().state == XMAX_READY);
    bool found = false;
    xmax_event event{};
    while (sdk.poll(event))
        if (event.type == XMAX_EVENT_ERROR) {
            CHECK(event.code == XMAX_SESSION_ERROR && std::string(event.message) == "Credits exhausted");
            found = true;
        }
    CHECK(found && heartbeat_requests(*http).size() == 1);
}

struct BlockingLeave : FakeRtc {
    std::atomic<bool> leaving{false}, released{false};

    void leave() noexcept override {
        leaving = true;
        while (!released)
            std::this_thread::sleep_for(1ms);
        FakeRtc::leave();
    }
};

static void server_close_does_not_wait_for_native_leave() {
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<BlockingLeave>();
    XmaxRealtimeManager sdk(api(http), [rtc] { return rtc; }, 10ms);
    ReleaseFlag release_leave{rtc->released};
    sdk.prepare({1280, 720, 30});
    sdk.start("prompt", "", false, false);
    auto closing = std::async(std::launch::async, [&] { sdk.disconnect(false); });
    const auto until = Clock::now() + 5s;
    while ((!rtc->leaving || http->count("DELETE /session/session-1") == 0) && Clock::now() < until)
        std::this_thread::sleep_for(1ms);
    const bool parallel = rtc->leaving && http->count("DELETE /session/session-1") == 1;
    rtc->released = true;
    closing.get();
    CHECK(parallel);
}

struct DelayedHeartbeat : FakeHttp {
    std::atomic<bool> entered{false}, release{false};

    HttpResponse request(const HttpRequest& input, const Cancel& cancel) override {
        if (input.method != "PUT")
            return FakeHttp::request(input, cancel);
        entered = true;
        while (!release)
            std::this_thread::sleep_for(1ms);
        // Simulate a response from a native HTTP phase that completes after cancellation.
        return {200,
                json{{"success", true}, {"data", {{"sessionUid", "session-1"}, {"status", "CLOSED"}, {"closeReason", "stale failure"}}}}
                    .dump()};
    }
};

static void cancelled_heartbeat_failure_is_discarded() {
    auto http = std::make_shared<DelayedHeartbeat>();
    auto rtc = std::make_shared<FakeRtc>();
    XmaxRealtimeManager sdk(api(http), [rtc] { return rtc; }, 5ms);
    ReleaseFlag release_heartbeat{http->release};
    sdk.prepare({1280, 720, 30});
    sdk.start("prompt", "", false, false);
    eventually([&] { return http->entered.load(); });
    auto closing = std::async(std::launch::async, [&] { sdk.disconnect(false); });
    const auto until = Clock::now() + 5s;
    while (http->count("DELETE /session/session-1") == 0 && Clock::now() < until)
        std::this_thread::sleep_for(1ms);
    const bool deleted = http->count("DELETE /session/session-1") == 1;
    http->release = true;
    closing.get();
    CHECK(deleted);
    xmax_event event{};
    while (sdk.poll(event))
        CHECK(event.type != XMAX_EVENT_ERROR);
    CHECK(sdk.state_snapshot().state == XMAX_READY);
}

int main() {
    try {
        first_request_grace_and_periodic_body();
        malformed_heartbeat_requires_session_identity();
        starts_after_generating_once_per_session();
        failed_start_has_no_billing_heartbeat();
        closure_reason_preserved();
        server_close_does_not_wait_for_native_leave();
        cancelled_heartbeat_failure_is_discarded();
        std::cout << "PASS generating-only heartbeats, startup grace, reconnect and parallel closure\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
