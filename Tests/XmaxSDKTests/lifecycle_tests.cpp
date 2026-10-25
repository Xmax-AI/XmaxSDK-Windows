#include "Core/Realtime/XmaxRealtimeFactory.hpp"
#include "Service/Media/MediaService.hpp"
#include "TestSupport.hpp"
#include <future>
#include <iostream>

using Lifecycle = XmaxRealtimeLifecycleManager;
using Kind = Lifecycle::OperationKind;
using Scope = Lifecycle::TerminationScope;
using namespace std::chrono_literals;

static void wait_for(const std::function<bool()>& ready) {
    const auto limit = Clock::now() + 5s;
    while (!ready() && Clock::now() < limit)
        std::this_thread::sleep_for(1ms);
    CHECK(ready());
}

struct Fixture {
    XmaxRealtimeErrorManager errors;
    std::vector<Scope> released;
    std::function<Lifecycle::CleanupResult(Scope)> release;
    Lifecycle lifecycle{[] {}};

    Fixture() {
        XmaxRealtimeFactory::assemble(lifecycle, errors, [this](Scope scope) {
            released.push_back(scope);
            if (release)
                return release(scope);
            return Lifecycle::CleanupResult{"session", scope != Scope::All, true};
        }, {});
        lifecycle.sync([&] { lifecycle.commit({XMAX_READY, "session", {}}); });
    }
    ~Fixture() {
        lifecycle.shutdown();
    }
};

static void scoped_cancellation_and_admission() {
    Fixture f;
    std::atomic<bool> entered{false};
    auto work = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] {
            f.lifecycle.run(Kind::Media, [&](const Lifecycle::Token& token) {
                token.failure_scope(Scope::All);
                entered = true;
                while (token.is_current())
                    std::this_thread::sleep_for(1ms);
                token.ensure_current();
            });
        });
    });
    wait_for([&] { return entered.load(); });
    expect_error(XMAX_INVALID_STATE, [&] { f.lifecycle.run(Kind::Connection, [](const auto&) {}); });
    CHECK(f.lifecycle.terminate(Scope::All));
    CHECK(work.wait_for(5s) == std::future_status::ready);
    work.get();
    CHECK(f.released == std::vector<Scope>{Scope::All});
    CHECK(f.lifecycle.snapshot().event.state == XMAX_IDLE);
}

static void replacement_keeps_connection() {
    Fixture f;
    std::atomic<bool> entered{false};
    const auto epoch = f.lifecycle.epoch();
    auto old = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] {
            f.lifecycle.run(Kind::Generation, [&](const Lifecycle::Token& token) {
                token.failure_scope(Scope::Connection);
                f.lifecycle.commit({XMAX_CONNECTED, "session", {}});
                entered = true;
                while (token.is_current())
                    std::this_thread::sleep_for(1ms);
                token.ensure_current();
            });
        });
    });
    wait_for([&] { return entered.load(); });
    f.lifecycle.run(Kind::Generation, [&](const auto& token) {
        CHECK(f.released == std::vector<Scope>{Scope::Generation});
        CHECK(f.lifecycle.state() == XMAX_CONNECTED);
        token.ensure_current();
        f.lifecycle.commit({XMAX_GENERATING, "session", "latest"});
    });
    old.get();
    CHECK(f.lifecycle.epoch() == epoch);
    CHECK(std::string(f.lifecycle.snapshot().event.task_id) == "latest");
}

static void cancelled_conditions_keep_generation() {
    Fixture f;
    f.lifecycle.sync([&] { f.lifecycle.commit({XMAX_GENERATING, "session", "task"}); });
    std::atomic<bool> entered{false};
    auto old = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] {
            f.lifecycle.run(Kind::Generation, [&](const auto& token) {
                token.failure_scope(Scope::Connection);
                entered = true;
                while (token.is_current())
                    std::this_thread::sleep_for(1ms);
                token.ensure_current();
            });
        });
    });
    wait_for([&] { return entered.load(); });
    f.lifecycle.run(Kind::Generation, [&](const auto&) {
        CHECK(f.lifecycle.state() == XMAX_GENERATING);
        CHECK(f.released.empty());
    });
    old.get();
    expect_error(XMAX_RTC_ERROR, [&] {
        f.lifecycle.run(Kind::Configuration, [](const auto&) { throw Error(XMAX_RTC_ERROR, "Rejected quality"); });
    });
    CHECK(f.released.empty() && f.lifecycle.state() == XMAX_GENERATING);
}

static void merged_termination_and_foreground_priority() {
    Fixture f;
    f.lifecycle.sync([&] { f.lifecycle.commit({XMAX_GENERATING, "session", "task"}); });
    std::atomic<bool> cleaning{false}, allow_cleanup{false};
    f.release = [&](Scope scope) {
        if (scope == Scope::Connection) {
            cleaning = true;
            while (!allow_cleanup)
                std::this_thread::sleep_for(1ms);
        }
        return Lifecycle::CleanupResult{"session", scope != Scope::All, true};
    };
    auto first = std::async(std::launch::async, [&] { return f.lifecycle.terminate(Scope::Connection); });
    wait_for([&] { return cleaning.load(); });
    const auto first_epoch = f.lifecycle.epoch();
    auto all = std::async(std::launch::async, [&] { return f.lifecycle.terminate(Scope::All); });
    wait_for([&] { return f.lifecycle.epoch() > first_epoch; });
    expect_error(XMAX_INVALID_STATE, [&] { f.lifecycle.run(Kind::Media, [](const auto&) {}); });
    allow_cleanup = true;
    CHECK(first.get() && all.get());
    CHECK(f.released == std::vector<Scope>({Scope::Connection, Scope::All}));
    CHECK(f.lifecycle.state() == XMAX_IDLE);

    f.release = {};
    f.lifecycle.sync([&] { f.lifecycle.commit({XMAX_GENERATING, "session", "task"}); });
    std::atomic<bool> foreground{false}, adaptive{false};
    f.lifecycle.run(Kind::Configuration, [&](const auto&) {
        f.lifecycle.enqueue_adaptive([&](const auto&) { adaptive = true; });
        foreground = true;
    });
    f.lifecycle.sync([] {});
    CHECK(!adaptive);
    std::promise<void> completed;
    auto completion = completed.get_future();
    f.lifecycle.enqueue_adaptive([&](const auto&) {
        CHECK(foreground);
        adaptive = true;
        completed.set_value();
    });
    CHECK(completion.wait_for(5s) == std::future_status::ready);
    CHECK(adaptive);
}

static void runtime_error_identity_and_stale_failures() {
    Fixture f;
    std::atomic<bool> entered{false};
    auto current = std::async(std::launch::async, [&] {
        try {
            f.lifecycle.run(Kind::Connection, [&](const auto& token) {
                token.failure_scope(Scope::Connection);
                entered = true;
                while (token.is_current())
                    std::this_thread::sleep_for(1ms);
                // A platform adapter may report cancellation when its wait is interrupted.
                throw Error(XMAX_CANCELLED, "Wait interrupted");
            });
            CHECK(false);
        } catch (const Error& error) {
            CHECK(error.code == XMAX_RTC_ERROR && error.api_code == 77 && error.http_status == 503);
        }
    });
    wait_for([&] { return entered.load(); });
    f.errors.forward(Error(XMAX_RTC_ERROR, "Room failed", 77, 503), Scope::Connection);
    current.get();
    f.lifecycle.sync([] {});
    CHECK(f.lifecycle.snapshot().error.api_code == 77);
    CHECK(f.released == std::vector<Scope>{Scope::Connection});
    f.errors.forward(Error(XMAX_MEDIA_ERROR, "Stale source"), Scope::All, [] { return false; });
    f.lifecycle.sync([] {});
    CHECK(f.released.size() == 1);

    XmaxRealtimeErrorManager routing;
    Cancel media, connection;
    routing.failure_handler([&](Error, Scope scope, Cancel valid) {
        (scope == Scope::All ? media : connection) = std::move(valid);
    });
    routing.forward(Error(XMAX_MEDIA_ERROR, "media"), Scope::All);
    routing.forward(Error(XMAX_RTC_ERROR, "room"), Scope::Connection);
    CHECK(media() && connection());
    routing.invalidate_pending_failures(Scope::Connection);
    CHECK(media() && !connection());
    routing.invalidate_pending_failures(Scope::All);
    CHECK(!media());
}

static void validation_does_not_own_cleanup() {
    Fixture f;
    expect_error(XMAX_INVALID_ARGUMENT, [&] {
        f.lifecycle.run(Kind::Media, [](const auto&) { MediaService::input({4096, 2160, 30}); });
    });
    CHECK(f.released.empty() && f.lifecycle.state() == XMAX_READY);
    // Frame layout validation is independent of a generation model's input budget.
    validate_format({4096, 2160, 30});
    expect_error(XMAX_INVALID_ARGUMENT, [] { MediaService::input({4096, 2160, 30}); });
    f.release = [](Scope) -> Lifecycle::CleanupResult { throw Error(XMAX_NETWORK_ERROR, "Close failed"); };
    CHECK(!f.lifecycle.terminate(Scope::Connection));
    CHECK(f.lifecycle.snapshot().error.code == XMAX_NETWORK_ERROR);
}

static void completion_is_staged_and_cancel_safe() {
    Fixture f;
    f.lifecycle.run(Kind::Media, [&](const auto& token) {
        f.lifecycle.commit({XMAX_PREPARING, "session", {}});
        f.lifecycle.complete({XMAX_READY, "session", {}}, token);
        CHECK(f.lifecycle.snapshot().event.state == XMAX_PREPARING);
    });
    CHECK(f.lifecycle.snapshot().event.state == XMAX_READY);
    xmax_event event{};
    while (f.lifecycle.poll(event)) {}

    std::atomic<bool> staged{false};
    auto preparation = std::async(std::launch::async, [&] {
        expect_error(XMAX_CANCELLED, [&] {
            f.lifecycle.run(Kind::Media, [&](const auto& token) {
                token.failure_scope(Scope::All);
                f.lifecycle.commit({XMAX_PREPARING, "session", {}});
                f.lifecycle.complete({XMAX_READY, "session", {}}, token);
                staged = true;
                while (token.is_current())
                    std::this_thread::sleep_for(1ms);
                token.ensure_current();
            });
        });
    });
    wait_for([&] { return staged.load(); });
    CHECK(f.lifecycle.terminate(Scope::All));
    preparation.get();
    while (f.lifecycle.poll(event))
        CHECK(event.type != XMAX_EVENT_STATE || event.state != XMAX_READY);
    CHECK(f.lifecycle.snapshot().event.state == XMAX_IDLE);
}

static void failed_generation_cleanup_releases_connection() {
    Fixture f;
    f.lifecycle.sync([&] { f.lifecycle.commit({XMAX_GENERATING, "session", "task"}); });
    f.release = [](Scope scope) {
        if (scope == Scope::Generation)
            throw Error(XMAX_RTC_ERROR, "Stop signaling failed");
        return Lifecycle::CleanupResult{"session", true, true};
    };
    CHECK(!f.lifecycle.terminate(Scope::Generation));
    CHECK(f.released == std::vector<Scope>({Scope::Generation, Scope::Connection}));
    CHECK(f.lifecycle.snapshot().event.state == XMAX_READY);
    CHECK(f.lifecycle.snapshot().error.code == XMAX_RTC_ERROR);
}

int main() {
    try {
        completion_is_staged_and_cancel_safe();
        failed_generation_cleanup_releases_connection();
        scoped_cancellation_and_admission();
        replacement_keeps_connection();
        cancelled_conditions_keep_generation();
        merged_termination_and_foreground_priority();
        runtime_error_identity_and_stale_failures();
        validation_does_not_own_cleanup();
        std::cout << "Lifecycle admission, replacement, scoped cleanup, priority, and failure routing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
