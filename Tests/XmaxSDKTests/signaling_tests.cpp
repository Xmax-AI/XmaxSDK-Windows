//
//  signaling_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Service/Realtime/RealtimeEventRelayService.hpp"
#include "Stream/Room/RoomCommandController.hpp"
#include "Stream/Room/RoomMessageSender.hpp"
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

static std::shared_future<void> ready(bool fails = false) {
    std::promise<void> value;
    if (fails)
        value.set_exception(std::make_exception_ptr(Error(XMAX_RTC_ERROR, "Synthetic send failure")));
    else
        value.set_value();
    return value.get_future().share();
}

struct Sender {
    std::mutex mutex;
    std::vector<json> messages;
    bool reject = false;
    std::shared_future<void> gate;

    std::shared_future<void> send(const json& message, Cancel cancel) {
        CHECK(!cancel());
        std::lock_guard<std::mutex> lock(mutex);
        messages.push_back(message);
        return gate.valid() ? gate : ready(reject);
    }

    size_t count() {
        std::lock_guard<std::mutex> lock(mutex);
        return messages.size();
    }
};

struct Release {
    std::promise<void>& promise;
    bool released = false;

    void release() {
        if (!released) {
            promise.set_value();
            released = true;
        }
    }

    ~Release() {
        release();
    }
};

static json command(const char* kind = "start") {
    return {{"event", kind}, {"prompt", "角色✨"}, {"uid", "task"}};
}

static std::string ack(const std::string& xid) {
    return json{{"event", "xid_ack"}, {"xid", xid}, {"count", 1}, {"user_id", "user"}}.dump();
}

static void retry_schedule_and_frozen_payload() {
    Sender sender;
    std::atomic<int64_t> clock{0};
    std::atomic<int> relays{0};
    json relayed;
    RoomCommandController controls(
        [&](const auto& value, auto cancel) { return sender.send(value, cancel); },
        [&](const auto& value, const auto&) {
            relayed = value;
            ++relays;
        },
        "user", "bot", [&] { return Clock::time_point{} + std::chrono::milliseconds(clock.load()); });
    auto ticket = controls.submit(command(), "task", [] { return false; });
    CHECK(ticket.completion.wait_for(2s) == std::future_status::ready);
    ticket.completion.get();
    eventually([&] { return relays == 1; });
    size_t count = 1;
    for (const auto time : {300, 1000, 3000, 3300, 4000}) {
        clock = time;
        eventually([&] { return sender.count() == count + 1; });
        ++count;
    }
    clock = 6000;
    std::this_thread::sleep_for(20ms);
    clock = 12000;
    std::this_thread::sleep_for(20ms);
    CHECK(sender.count() == 6 && relays == 1);
    controls.close();
    for (const auto& sent : sender.messages)
        CHECK(sent == relayed && sent["xid"] == ticket.xid);
}

static void receipts_do_not_complete_original_or_cancel_http() {
    Sender sender;
    std::promise<void> original;
    sender.gate = original.get_future().share();
    std::atomic<int64_t> clock{0};
    std::atomic<int> relays{0}, relay_cancelled{0};
    RoomCommandController controls(
        [&](const auto& value, auto cancel) { return sender.send(value, cancel); },
        [&](const auto&, const Cancel& cancel) {
            ++relays;
            while (!cancel())
                std::this_thread::sleep_for(1ms);
            ++relay_cancelled;
        },
        "user", "bot", [&] { return Clock::time_point{} + std::chrono::milliseconds(clock.load()); });
    Release release{original};
    auto ticket = controls.submit(command(), "task", [] { return false; });
    eventually([&] { return sender.count() == 1 && relays == 1; });
    controls.receive("bot", ack(ticket.xid));
    CHECK(ticket.completion.wait_for(20ms) == std::future_status::timeout);
    clock = 12000;
    std::this_thread::sleep_for(20ms);
    CHECK(sender.count() == 1 && relay_cancelled == 0);
    release.release();
    CHECK(ticket.completion.wait_for(2s) == std::future_status::ready);
    ticket.completion.get();
    controls.cancel("task");
    eventually([&] { return relay_cancelled == 1; });
    controls.close();
}

static void receipt_validation_and_reassembly() {
    Sender sender;
    std::atomic<int64_t> clock{0};
    RoomCommandController controls(
        [&](const auto& value, auto cancel) { return sender.send(value, cancel); }, {}, "user", "bot",
        [&] { return Clock::time_point{} + std::chrono::milliseconds(clock.load()); });
    auto ticket = controls.submit(command(), "task", [] { return false; });
    CHECK(ticket.completion.wait_for(2s) == std::future_status::ready);
    ticket.completion.get();
    controls.receive("stranger", ack(ticket.xid));
    controls.receive("user", ack(ticket.xid));
    controls.receive("bot", ack("old-xid"));
    for (const json invalid :
         {json(0), json(-1), json(1.5), json(true), json("1"), json(9007199254740992ULL)}) {
        auto receipt = json::parse(ack(ticket.xid));
        receipt["count"] = invalid;
        controls.receive("bot", receipt.dump());
    }
    auto wrong = json::parse(ack(ticket.xid));
    wrong["user_id"] = "another-user";
    controls.receive("bot", wrong.dump());
    clock = 300;
    eventually([&] { return sender.count() == 2; });
    auto nullable_recipient = json::parse(ack(ticket.xid));
    nullable_recipient["user_id"] = nullptr;
    const auto text = nullable_recipient.dump();
    auto part = [&](int index) {
        return json{{"event", "__trtc_chunk__"},
                    {"eventId", "receipt"},
                    {"index", index},
                    {"count", 2},
                    {"data", index ? text.substr(text.size() / 2) : text.substr(0, text.size() / 2)}}
            .dump();
    };
    controls.receive("bot", part(1));
    controls.receive("stranger", part(0));
    controls.receive("bot", part(0));
    clock = 4000;
    std::this_thread::sleep_for(30ms);
    CHECK(sender.count() == 2);
    controls.close();
    controls.receive("bot", ack(ticket.xid));
    expect_error(XMAX_CANCELLED, [&] { controls.submit(command(), "task", [] { return false; }); });
}

static void supersession_failure_and_cancellation() {
    Sender sender;
    std::atomic<int64_t> clock{0};
    RoomCommandController controls(
        [&](const auto& value, auto cancel) { return sender.send(value, cancel); }, {}, "user", "bot",
        [&] { return Clock::time_point{} + std::chrono::milliseconds(clock.load()); });
    auto start = controls.submit(command(), "task", [] { return false; });
    start.completion.get();
    auto old = controls.submit(command("change_condition"), "task", [] { return false; });
    old.completion.get();
    auto newest = controls.submit(command("change_condition"), "task", [] { return false; });
    newest.completion.get();
    const auto before = sender.count();
    clock = 300;
    eventually([&] { return sender.count() == before + 2; }); // start and latest condition only
    controls.cancel("task");
    clock = 1000;
    std::this_thread::sleep_for(20ms);
    CHECK(sender.count() == before + 2);
    controls.close();

    Sender failed;
    failed.reject = true;
    RoomCommandController rejecting(
        [&](const auto& value, auto cancel) { return failed.send(value, cancel); }, {}, "user", "bot");
    auto initial = rejecting.submit(command(), "task", [] { return false; });
    CHECK(initial.completion.wait_for(2s) == std::future_status::ready);
    expect_error(XMAX_RTC_ERROR, [&] { initial.completion.get(); });
    rejecting.close();
    CHECK(failed.count() == 1);
}

static void deadline_retains_initial_and_retry_failures_are_nonfatal() {
    Sender sender;
    std::promise<void> original;
    sender.gate = original.get_future().share();
    std::atomic<int64_t> clock{0};
    RoomCommandController controls(
        [&](const auto& value, auto cancel) { return sender.send(value, cancel); }, {}, "user", "bot",
        [&] { return Clock::time_point{} + std::chrono::milliseconds(clock.load()); });
    Release release{original};
    auto ticket = controls.submit(command(), "task", [] { return false; });
    eventually([&] { return sender.count() == 1; });
    clock = 6000;
    CHECK(ticket.completion.wait_for(30ms) == std::future_status::timeout);
    CHECK(sender.count() == 1);
    release.release();
    ticket.completion.get();
    controls.close();

    Sender retry;
    clock = 0;
    RoomCommandController errors(
        [&](const auto& value, auto cancel) { return retry.send(value, cancel); },
        [](const auto&, const auto&) { throw Error(XMAX_NETWORK_ERROR, "HTTP unavailable"); }, "user", "bot",
        [&] { return Clock::time_point{} + std::chrono::milliseconds(clock.load()); });
    auto sent = errors.submit(command(), "task", [] { return false; });
    sent.completion.get();
    {
        std::lock_guard<std::mutex> lock(retry.mutex);
        retry.reject = true;
    }
    clock = 300;
    eventually([&] { return retry.count() == 2; });
    clock = 1000;
    eventually([&] { return retry.count() == 3; });
    sent.completion.get(); // HTTP and redundant RTC failures cannot retract the successful initial send.
    errors.receive("bot", ack(sent.xid));
    errors.close();
}

static void decoder_bounds_and_sender_isolation() {
    RoomMessageDecoder decoder;
    CHECK(!decoder.decode("a", "bad json"));
    auto part = [](const char* id, int index, const std::string& data) {
        return json{
            {"event", "__agora_chunk__"}, {"eventId", id}, {"index", index}, {"count", 2}, {"data", data}}
            .dump();
    };
    CHECK(!decoder.decode("a", part("same", 0, "{\"event\":")));
    CHECK(!decoder.decode("b", part("same", 1, "\"xid_ack\"}")));
    CHECK(decoder.decode("a", part("same", 1, "\"xid_ack\"}"))->at("event") == "xid_ack");
    CHECK(!decoder.decode("a", part("huge", 0, std::string(262000, 'x'))));
    CHECK(!decoder.decode("a", part("huge", 1, std::string(200, 'y'))));
    for (int i = 0; i < 1001; ++i)
        decoder.decode("a", part(std::to_string(i).c_str(), 0, "{\"event\":"));
    CHECK(!decoder.decode("a", part("0", 1, "\"xid_ack\"}")));
    CHECK(decoder.decode("a", part("1000", 1, "\"xid_ack\"}")));
}

static void native_queue_cancellation_and_recovery() {
    auto rtc = std::make_shared<FakeRtc>();
    RoomMessageSender sender;
    std::atomic<bool> cancelled{false};
    auto long_send =
        sender.enqueue(rtc, {{"prompt", std::string(100000, 'x')}}, [&] { return cancelled.load(); });
    eventually([&] { return rtc->messages_count() > 0; });
    cancelled = true;
    CHECK(long_send.wait_for(2s) == std::future_status::ready);
    expect_error(XMAX_CANCELLED, [&] { long_send.get(); });
    sender.reset();
    auto next = sender.enqueue(rtc, {{"event", "stop"}}, [] { return false; });
    CHECK(next.wait_for(2s) == std::future_status::ready);
    next.get();
}

struct RelayHttp : Http {
    HttpRequest last;
    json receipt;

    HttpResponse request(const HttpRequest& input, const Cancel&) override {
        last = input;
        return {200, json{{"success", true}, {"data", receipt}}.dump()};
    }
};

static void relay_receipts_and_routes() {
    for (const bool global : {false, true}) {
        auto http = std::make_shared<RelayHttp>();
        Config config;
        config.key = "test";
        config.environment = global ? XMAX_ENVIRONMENT_GLOBAL : XMAX_ENVIRONMENT_CHINA;
        RealtimeEventRelayService relay(std::make_shared<ApiService>(config, http));
        auto message = command();
        message["xid"] = "logical-command";
        for (bool accepted : {false, true}) {
            http->receipt = {{"xid", "logical-command"}, {"accepted", accepted}};
            relay.relay("session", "model", message, [] { return false; });
            CHECK(http->last.method == "POST" &&
                  http->last.url == config.base_url(true) + "/client-event/relay");
            CHECK(json::parse(http->last.body) ==
                  json({{"sessionId", "session"}, {"modelId", "model"}, {"data", message}}));
        }
        for (const auto receipt :
             {json{{"xid", "other"}, {"accepted", true}}, json{{"xid", "logical-command"}, {"accepted", 1}},
              json{{"xid", "logical-command"}}}) {
            http->receipt = receipt;
            expect_error(XMAX_API_ERROR,
                         [&] { relay.relay("session", "model", message, [] { return false; }); });
        }
        http->last.url.clear();
        relay.relay("session", "", message, [] { return false; });
        CHECK(http->last.url.empty());
    }
}

int main() {
    try {
        retry_schedule_and_frozen_payload();
        receipts_do_not_complete_original_or_cancel_http();
        receipt_validation_and_reassembly();
        supersession_failure_and_cancellation();
        deadline_retains_initial_and_retry_failures_are_nonfatal();
        decoder_bounds_and_sender_isolation();
        native_queue_cancellation_and_recovery();
        relay_receipts_and_routes();
        std::cout << "PASS command retries, receipts, relay, cancellation and room isolation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
