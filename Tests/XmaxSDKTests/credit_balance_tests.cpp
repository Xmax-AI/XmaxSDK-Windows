//
//  credit_balance_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "CAPI/ClientHandle.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <iostream>
#include <xmax/xmax.hpp>

struct BalanceHttp : Http {
    HttpResponse response{200, R"({"success":true,"data":{"currentBalance":1250.75}})"};
    std::vector<HttpRequest> requests;
    bool network_failure = false, cancel_in_transport = false;

    HttpResponse request(const HttpRequest& input, const Cancel& cancel) override {
        requests.push_back(input);
        if (cancel_in_transport) {
            while (!cancel())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            throw Error(XMAX_CANCELLED, "Cancelled during transport");
        }
        if (network_failure)
            throw Error(XMAX_NETWORK_ERROR, "Offline");
        return response;
    }
};

static Config configuration(xmax_environment environment = XMAX_ENVIRONMENT_CHINA) {
    Config config;
    config.key = "  balance-key\n";
    config.environment = environment;
    return config;
}

static void success_and_environment() {
    for (auto region : {XMAX_ENVIRONMENT_CHINA, XMAX_ENVIRONMENT_GLOBAL}) {
        auto http = std::make_shared<BalanceHttp>();
        XmaxClient client(configuration(region), http);
        CHECK(client.credit_balance([] { return false; }) == 1250.75);
        const auto& request = http->requests.back();
        CHECK(request.method == "GET" && request.body.empty());
        CHECK(request.url == (region == XMAX_ENVIRONMENT_CHINA
                                  ? "https://cloud.xmax.22duck.cn/open/api/v1/balance"
                                  : "https://api.xmax.cloud/open/api/v1/balance"));
        CHECK(std::find(request.headers.begin(), request.headers.end(),
                        std::make_pair(std::string("X-Api-Key"), std::string("balance-key"))) !=
              request.headers.end());
        http->response.body = R"({"success":true,"data":{"currentBalance":0}})";
        CHECK(client.credit_balance([] { return false; }) == 0);
        http->response.body = R"({"success":true,"data":{"currentBalance":25.5}})";
        CHECK(client.credit_balance([] { return false; }) == 25.5 && http->requests.size() == 3);
    }
}

static void invalid_payloads_and_details() {
    auto http = std::make_shared<BalanceHttp>();
    XmaxClient client(configuration(), http);
    for (const auto* data : {"null", "{}", R"({"currentBalance":null})", R"({"currentBalance":true})",
                             R"({"currentBalance":"1250.75"})", R"({"currentBalance":-1})",
                             R"({"currentBalance":1e400})", R"({"currentBalance":[]})"}) {
        http->response.body = std::string(R"({"success":true,"data":)") + data + "}";
        try {
            client.credit_balance([] { return false; });
            CHECK(false);
        } catch (const Error& error) {
            CHECK(error.code == XMAX_API_ERROR && error.http_status == 200);
        }
    }
    for (int status : {200, 401, 429}) {
        http->response = {status,
                          R"({"success":false,"code":46002,"message":"Invalid API key","data":null})"};
        try {
            client.credit_balance([] { return false; });
            CHECK(false);
        } catch (const Error& error) {
            CHECK(error.code == XMAX_API_ERROR && error.http_status == status && error.api_code == 46002 &&
                  std::string(error.what()) == "Invalid API key");
        }
    }
    http->network_failure = true;
    expect_error(XMAX_NETWORK_ERROR, [&] { client.credit_balance([] { return false; }); });
}

static void missing_key_and_cancellation() {
    auto http = std::make_shared<BalanceHttp>();
    auto config = configuration();
    config.key = " \n\t ";
    XmaxClient empty(config, http);
    expect_error(XMAX_INVALID_API_KEY, [&] { empty.credit_balance([] { return false; }); });
    CHECK(http->requests.empty());
    XmaxClient client(configuration(), http);
    expect_error(XMAX_CANCELLED, [&] { client.credit_balance([] { return true; }); });
    CHECK(http->requests.empty());
    http->cancel_in_transport = true;
    std::atomic<bool> cancelled{false};
    std::thread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cancelled = true;
    });
    expect_error(XMAX_CANCELLED, [&] { client.credit_balance([&] { return cancelled.load(); }); });
    cancel.join();
    CHECK(http->requests.size() == 1);
}

static void public_boundaries() {
    auto http = std::make_shared<BalanceHttp>();
    xmax::client client("");
    client.get()->client = XmaxClient(configuration(), http);
    CHECK(client.credit_balance().current_balance == 1250.75);
    xmax_credit_balance balance{sizeof(balance), -1};
    CHECK(xmax_client_get_credit_balance(client.get(), &balance, nullptr) == XMAX_OK);
    CHECK(balance.current_balance == 1250.75);
    CHECK(xmax_client_get_credit_balance(nullptr, &balance, nullptr) == XMAX_INVALID_ARGUMENT);
    CHECK(xmax_client_get_credit_balance(client.get(), nullptr, nullptr) == XMAX_INVALID_ARGUMENT);
    balance.struct_size = 0;
    CHECK(xmax_client_get_credit_balance(client.get(), &balance, nullptr) == XMAX_INVALID_ARGUMENT);
    balance = {sizeof(balance), -1};
    xmax_request_cancellation invalid{sizeof(invalid), nullptr, nullptr};
    CHECK(xmax_client_get_credit_balance(client.get(), &balance, &invalid) == XMAX_INVALID_ARGUMENT);
    const size_t previous = http->requests.size();
    const xmax_request_cancellation cancelled{sizeof(cancelled), [](void*) -> int32_t { return 1; }, nullptr};
    CHECK(xmax_client_get_credit_balance(client.get(), &balance, &cancelled) == XMAX_CANCELLED);
    CHECK(balance.current_balance == -1 && http->requests.size() == previous);
    try {
        client.credit_balance([] { return true; });
        CHECK(false);
    } catch (const xmax::error& error) {
        CHECK(error.code() == XMAX_CANCELLED);
    }
    http->response = {401, R"({"success":false,"code":46002,"message":"Invalid API key","data":null})"};
    CHECK(xmax_client_get_credit_balance(client.get(), &balance, nullptr) == XMAX_API_ERROR);
    xmax_error_info error{};
    error.struct_size = sizeof(error);
    CHECK(xmax_last_error_info(&error) == XMAX_OK);
    CHECK(error.code == XMAX_API_ERROR && error.http_status == 401 && error.api_code == 46002 &&
          balance.current_balance == -1);
    http->response = {200, R"({"success":true,"data":{"currentBalance":null}})"};
    try {
        client.credit_balance();
        CHECK(false);
    } catch (const xmax::error& error) {
        CHECK(error.code() == XMAX_API_ERROR && error.http_status() == 200);
    }
}

int main() {
    try {
        public_boundaries();
        success_and_environment();
        invalid_payloads_and_details();
        missing_key_and_cancellation();
        std::cout << "PASS credit balance environments, API-key authentication, fractions, zero, freshness, "
                     "invalid payloads, server errors and cancellation; no real network/capture/session\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
