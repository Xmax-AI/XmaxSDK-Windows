#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Core/Storage/XmaxStorageManager.hpp"
#include "Foundation/RTC/RtcStatsLogger.hpp"
#include "Service/Network/ApiService.hpp"
#include "TestSupport.hpp"
#include <cstring>
#include <iostream>

static std::vector<xmax_log_record> drain() {
    std::vector<xmax_log_record> records;
    xmax_log_record value{};
    while (XmaxLogger::poll(value))
        records.push_back(value);
    return records;
}
static std::string messages(const std::vector<xmax_log_record>& records) {
    std::string text;
    for (const auto& record : records)
        text += std::string(record.message) + "\n";
    return text;
}
static void defaults_and_filtering() {
    CHECK(XmaxLogger::configuration().options == XMAX_LOG_NONE);
    bool evaluated = false;
    XmaxLogger::write(LogCategory::API, XMAX_LOG_ERROR, [&](auto) {
        evaluated = true;
        return "off";
    });
    CHECK(!evaluated && drain().empty());
    XmaxLogger::configure(XMAX_LOG_BUSINESS, XMAX_LOG_ENGLISH);
    CHECK(XmaxLogger::is_enabled(XMAX_LOG_BUSINESS));
    CHECK(!XmaxLogger::is_enabled(0) && !XmaxLogger::is_enabled(XMAX_LOG_ALL));
    XmaxLogger::write(
        LogCategory::RTC, XMAX_LOG_DEBUG,
        [&](auto) {
            evaluated = true;
            return "metrics";
        },
        XMAX_LOG_PERFORMANCE);
    CHECK(!evaluated);
    for (auto level : {XMAX_LOG_DEBUG, XMAX_LOG_INFO, XMAX_LOG_WARNING, XMAX_LOG_ERROR})
        XmaxLogger::write(LogCategory::API, level,
                          [](auto language) { return XmaxLogger::localized(language, "中文", "English"); });
    const auto records = drain();
    CHECK(records.size() == 4);
    for (size_t i = 0; i < records.size(); ++i) {
        CHECK(int(records[i].level) == int(i));
        CHECK(records[i].timestamp_us > 0 && records[i].sequence > 0);
        CHECK(std::string(records[i].message) == "[Xmax][API] English");
    }
    expect_error(XMAX_INVALID_ARGUMENT, [] { XmaxLogger::configure(16, XMAX_LOG_CHINESE); });
    expect_error(XMAX_INVALID_ARGUMENT, [] { XmaxLogger::configure(0, static_cast<xmax_log_language>(5)); });
    CHECK(XmaxLogger::configuration().options == XMAX_LOG_BUSINESS);
}
static void categories_localization_and_bounds() {
    XmaxLogger::configure(XMAX_LOG_ALL, XMAX_LOG_CHINESE);
    for (auto category : {LogCategory::Realtime, LogCategory::RTC, LogCategory::Media, LogCategory::API,
                          LogCategory::Storage, LogCategory::Room, LogCategory::Stream, LogCategory::Render,
                          LogCategory::Interaction, LogCategory::Permission})
        XmaxLogger::write(category, XMAX_LOG_INFO, [](auto language) {
            return XmaxLogger::localized(language, "第一行\n第二行", "first\nsecond");
        });
    for (const auto& record : drain()) {
        const std::string prefix = std::string("[Xmax][") + record.category + "] ";
        CHECK(std::string(record.message) == prefix + "第一行\n" + prefix + "第二行");
    }
    XmaxLogger::write(LogCategory::Media, XMAX_LOG_INFO, [](auto) {
        std::string text;
        for (int i = 0; i < 4000; ++i)
            text += "摄像头";
        return text;
    });
    auto records = drain();
    CHECK(records.size() == 1 && std::strlen(records[0].message) < sizeof(records[0].message));
    CHECK(std::string(records[0].message).find("[truncated]") != std::string::npos);
    CHECK(!json(std::string(records[0].message)).dump().empty()); // Throws on a split UTF-8 code point.
    XmaxLogger::write(LogCategory::API, XMAX_LOG_INFO,
                      [](auto) { return std::string("one\r\ninjected\0text", 18); });
    CHECK(messages(drain()).find('\r') == std::string::npos);
}
static void redaction() {
    XmaxLogger::configure(XMAX_LOG_ALL, XMAX_LOG_ENGLISH);
    const auto key = XmaxLogger::protect_secret("API_KEY_SENTINEL_92817");
    const auto token = XmaxLogger::protect_secret("RTC_TOKEN_SENTINEL_42317");
    XmaxLogger::write(LogCategory::RTC, XMAX_LOG_ERROR, [](auto) {
        return "Failure API_KEY_SENTINEL_92817 RTC_TOKEN_SENTINEL_42317\nAuthorization: Bearer "
               "UNKNOWN_SENTINEL\n"
               "api_key=OTHER_SENTINEL token=THIRD_SENTINEL\nhttps://cdn.test/a?signature=SIGNED_SENTINEL";
    });
    const auto text = messages(drain());
    for (const auto* secret : {"API_KEY_SENTINEL_92817", "RTC_TOKEN_SENTINEL_42317", "UNKNOWN_SENTINEL",
                               "OTHER_SENTINEL", "THIRD_SENTINEL", "SIGNED_SENTINEL"})
        CHECK(text.find(secret) == std::string::npos);
    const auto body = XmaxLogger::failure_body(
        R"({"message":"API_KEY_SENTINEL_92817 failed","credentials":{"accessKeyId":"HIDDEN"},"nested":{"room_token":"HIDDEN2"},"prompt":"PRIVATE_PROMPT","data":{"value":"PAYLOAD"}})");
    CHECK(body.find("API_KEY_SENTINEL_92817") == std::string::npos &&
          body.find("HIDDEN") == std::string::npos);
    CHECK(body.find("PRIVATE_PROMPT") == std::string::npos && body.find("PAYLOAD") == std::string::npos);
    CHECK(XmaxLogger::failure_body("<html>SECRET</html>").find("SECRET") == std::string::npos);
    CHECK(XmaxLogger::failure_body(R"(["OPAQUE_SECRET"])").find("OPAQUE_SECRET") == std::string::npos);
    CHECK(XmaxLogger::failure_body(R"({"unknownPayload":"OPAQUE_SECRET"})").find("OPAQUE_SECRET") ==
          std::string::npos);
    CHECK(XmaxLogger::failure_body(R"({"error":"{\"token\":\"NESTED_SECRET\"}"})").find("NESTED_SECRET") ==
          std::string::npos);
    CHECK(XmaxLogger::failure_body(std::string(65537, 'a')).find("omitted") != std::string::npos);
    const auto overlapping = XmaxLogger::protect_secret("token");
    const auto punctuation = XmaxLogger::protect_secret("EXOTIC space,quoted\"value");
    const auto overlap =
        XmaxLogger::redact("room_token=UNREGISTERED_SECRET\nAPI-Key=EXOTIC space,quoted\"value");
    CHECK(overlap.find("UNREGISTERED_SECRET") == std::string::npos);
    CHECK(overlap.find("EXOTIC") == std::string::npos && overlap.find("quoted") == std::string::npos);
    const auto malformed = XmaxLogger::redact(std::string("invalid \xff UTF8"));
    CHECK(!json(malformed).dump().empty());
}
struct DiagnosticHttp : Http {
    HttpResponse response{200, R"({"success":true,"data":{"token":"SUCCESS_PAYLOAD_SENTINEL"}})"};
    bool fail = false;
    HttpResponse request(const HttpRequest&, const Cancel&) override {
        if (fail)
            throw Error(XMAX_NETWORK_ERROR, "WinHTTP 12007: DNS lookup failed");
        return response;
    }
};
static void api_diagnostics() {
    XmaxLogger::configure(XMAX_LOG_BUSINESS, XMAX_LOG_ENGLISH);
    auto http = std::make_shared<DiagnosticHttp>();
    ApiService api({"API_KEY_SENTINEL_92817", "x2.1", 15000}, http);
    auto result = api.request("GET", "/sample?api_key=QUERY_SENTINEL", nullptr, [] { return false; });
    CHECK(result["token"] == "SUCCESS_PAYLOAD_SENTINEL");
    auto text = messages(drain());
    CHECK(text.find("GET /sample") != std::string::npos && text.find("Status: 200") != std::string::npos);
    CHECK(text.find("Duration:") != std::string::npos &&
          text.find("SUCCESS_PAYLOAD_SENTINEL") == std::string::npos);
    CHECK(text.find("QUERY_SENTINEL") == std::string::npos);
    http->response = {
        503,
        R"({"success":false,"message":"API_KEY_SENTINEL_92817: backend unavailable","room_token":"TOKEN_SENTINEL"})"};
    expect_error(XMAX_API_ERROR, [&] { api.request("GET", "/failed", nullptr, [] { return false; }); });
    auto records = drain();
    CHECK(records.size() == 1 && records[0].level == XMAX_LOG_ERROR);
    text = messages(records);
    CHECK(text.find("503") != std::string::npos && text.find("backend unavailable") != std::string::npos);
    CHECK(text.find("API_KEY_SENTINEL_92817") == std::string::npos &&
          text.find("TOKEN_SENTINEL") == std::string::npos);
    http->response = {200, R"({"success":false,"message":"business rejection"})"};
    expect_error(XMAX_API_ERROR, [&] { api.request("POST", "/business", nullptr, [] { return false; }); });
    CHECK(messages(drain()).find("business rejection") != std::string::npos);
    http->fail = true;
    expect_error(XMAX_NETWORK_ERROR, [&] { api.request("GET", "/network", nullptr, [] { return false; }); });
    text = messages(drain());
    CHECK(text.find("12007") != std::string::npos && text.find("Request Failed") != std::string::npos);
}
static void rtc_statistics() {
    XmaxLogger::configure(XMAX_LOG_PERFORMANCE, XMAX_LOG_ENGLISH);
    xmax_event e{};
    e.type = XMAX_EVENT_STATISTICS;
    e.width = 1920;
    e.height = 1024;
    e.fps = 29;
    e.bitrate_kbps = 1100;
    e.packet_loss_percent = 2;
    e.e2e_ms = -1;
    e.quality = -1;
    RtcStatsLogger::video(e, 31, 30);
    auto text = messages(drain());
    CHECK(text.find("Capture Frame Rate: 31 fps") != std::string::npos);
    CHECK(text.find("Encode Frame Rate: 30 fps") != std::string::npos);
    e.direction = 1;
    RtcStatsLogger::video(e, -1, 28);
    text = messages(drain());
    CHECK(text.find("Decode Frame Rate: 28 fps") != std::string::npos &&
          text.find("End-to-End Delay: —") != std::string::npos);
    e.type = XMAX_EVENT_PERFORMANCE;
    e.code = 1;
    RtcStatsLogger::event(e);
    auto records = drain();
    CHECK(records.size() == 1 && records[0].level == XMAX_LOG_WARNING);
    e.type = XMAX_EVENT_TIMING;
    RtcStatsLogger::event(e);
    CHECK(drain().empty()); // Startup timing is business, not performance.
    XmaxLogger::configure(XMAX_LOG_BUSINESS, XMAX_LOG_CHINESE);
    RtcStatsLogger::event(e);
    text = messages(drain());
    CHECK(text.find("创建会话：") != std::string::npos && text.find("总启动耗时：") != std::string::npos);
    const auto token = XmaxLogger::protect_secret("RTC_ERROR_TOKEN_SENTINEL");
    const auto error = RtcStatsLogger::error_message(109, "Token expired: RTC_ERROR_TOKEN_SENTINEL");
    CHECK(error.find("109") != std::string::npos && error.find("Token expired") != std::string::npos);
    CHECK(error.find("RTC_ERROR_TOKEN_SENTINEL") == std::string::npos);
}
struct DiagnosticStorageHttp : Http {
    bool fail = false;
    HttpResponse request(const HttpRequest& input, const Cancel&) override {
        if (input.url.find("/cos/sts") != std::string::npos) {
            const json credentials{{"accessKeyId", "COS_ID_SENTINEL"},
                                   {"secretAccessKey", "COS_SECRET_SENTINEL"},
                                   {"sessionToken", "COS_TOKEN_SENTINEL"}};
            const json data{{"bucket", "demo-123"},
                            {"region", "ap-test"},
                            {"prefix", "uploads/"},
                            {"credentials", credentials}};
            return {200, json{{"success", true}, {"data", data}}.dump()};
        }
        if (fail)
            throw Error(XMAX_NETWORK_ERROR, "WinHTTP 12030: COS_SECRET_SENTINEL COS_TOKEN_SENTINEL");
        return {200, "", {{"ETag", "etag"}}};
    }
};
static void storage_diagnostics() {
    XmaxLogger::configure(XMAX_LOG_BUSINESS, XMAX_LOG_ENGLISH);
    auto http = std::make_shared<DiagnosticStorageHttp>();
    auto api =
        std::make_shared<ApiService>(Config{"STORAGE_API_KEY_SENTINEL", "x2.1", 15000}, http);
    XmaxStorageManager storage(api);
    const uint8_t bytes[]{1, 2, 3};
    storage.upload_data(bytes, sizeof(bytes), "image.png", "image/png", false);
    auto text = messages(drain());
    CHECK(text.find("[Xmax][Storage]") != std::string::npos && text.find("HTTP 200") != std::string::npos);
    for (const auto* secret :
         {"STORAGE_API_KEY_SENTINEL", "COS_ID_SENTINEL", "COS_SECRET_SENTINEL", "COS_TOKEN_SENTINEL"})
        CHECK(text.find(secret) == std::string::npos);
    http->fail = true;
    try {
        storage.upload_data(bytes, sizeof(bytes), "image.png", "image/png", false);
        CHECK(false);
    } catch (const Error& error) {
        CHECK(error.code == XMAX_NETWORK_ERROR);
        CHECK(std::string(error.what()).find("12030") != std::string::npos);
        CHECK(std::string(error.what()).find("COS_SECRET_SENTINEL") == std::string::npos);
    }
    text = messages(drain());
    CHECK(text.find("12030") != std::string::npos);
    CHECK(text.find("COS_SECRET_SENTINEL") == std::string::npos &&
          text.find("COS_TOKEN_SENTINEL") == std::string::npos);
}
static void lifecycle_and_signal_privacy() {
    XmaxLogger::configure(XMAX_LOG_BUSINESS, XMAX_LOG_ENGLISH);
    auto http = std::make_shared<FakeHttp>();
    auto rtc = std::make_shared<FakeRtc>();
    auto api =
        std::make_shared<ApiService>(Config{"API_KEY_SENTINEL_92817", "x2.1", 15000}, http);
    {
        XmaxRealtimeManager session(api, [rtc] { return rtc; });
        session.prepare({1920, 1024, 30});
        session.start("PRIVATE_PROMPT_SENTINEL", "https://ref.test/a?token=REFERENCE_SENTINEL", false, false);
        session.update("UPDATED_PRIVATE_PROMPT", "", false);
        session.disconnect(true);
    }
    const auto text = messages(drain());
    for (const auto* expected : {"[Xmax][Media]", "[Xmax][Realtime]", "[Xmax][Stream]", "[Xmax][Room]",
                                 "[Xmax][API]", "Realtime Launch Timing"})
        CHECK(text.find(expected) != std::string::npos);
    for (const auto* secret : {"PRIVATE_PROMPT_SENTINEL", "UPDATED_PRIVATE_PROMPT", "REFERENCE_SENTINEL",
                               "API_KEY_SENTINEL_92817"})
        CHECK(text.find(secret) == std::string::npos);
}
static void concurrency_and_bounded_queue() {
    XmaxLogger::configure(XMAX_LOG_ALL, XMAX_LOG_ENGLISH);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([] {
            for (int j = 0; j < 100; ++j)
                XmaxLogger::write(LogCategory::Stream, XMAX_LOG_DEBUG, [](auto) { return "concurrent"; });
        });
    for (auto& thread : threads)
        thread.join();
    auto records = drain();
    CHECK(records.size() == 256);
    for (size_t i = 1; i < records.size(); ++i)
        CHECK(records[i].sequence == records[i - 1].sequence + 1);
    XmaxLogger::write(LogCategory::API, XMAX_LOG_ERROR,
                      [](auto) -> std::string { throw std::runtime_error("format failure"); });
    CHECK(drain().empty());
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop)
            XmaxLogger::write(LogCategory::API, XMAX_LOG_INFO, [](auto) { return "toggle"; });
    });
    for (int i = 0; i < 50; ++i)
        XmaxLogger::configure(i % 2 ? XMAX_LOG_ALL : XMAX_LOG_NONE, XMAX_LOG_CHINESE);
    XmaxLogger::configure(XMAX_LOG_NONE, XMAX_LOG_ENGLISH);
    stop = true;
    writer.join();
    CHECK(drain().empty());
}
int main() {
    try {
        defaults_and_filtering();
        categories_localization_and_bounds();
        redaction();
        api_diagnostics();
        rtc_statistics();
        storage_diagnostics();
        lifecycle_and_signal_privacy();
        concurrency_and_bounded_queue();
        std::cout << "Logger filtering, localization, privacy, API/RTC diagnostics, lifecycle and "
                     "concurrency passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
