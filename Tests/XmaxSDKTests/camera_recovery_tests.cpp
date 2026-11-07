//
//  camera_recovery_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "TestSupport.hpp"
#include <condition_variable>
#include <future>
#include <iostream>

/**
 * The native capture behavior for one camera configuration attempt.
 */
enum class CaptureAttempt {

    /**
     * Delivers a valid preview frame.
     */
    Ready,

    /**
     * Throws before capture becomes ready.
     */
    Throw,

    /**
     * Reports a capture failure from a native worker thread without delivering a frame.
     */
    AsyncFailure,

    /**
     * Delivers a frame and reports failure before native startup returns.
     */
    FrameThenFailure,

    /**
     * Delivers an invalid frame during startup.
     */
    InvalidFrame,

    /**
     * Leaves startup pending until its caller cancels.
     */
    Wait
};

/**
 * Stores scripted capture attempts and the active native callbacks without using a physical camera.
 */
struct CaptureScript {

    /**
     * Protects the attempt count and active callbacks shared with the test thread.
     */
    std::mutex mutex;

    /**
     * Notifies the test when a capture attempt starts.
     */
    std::condition_variable changed;

    /**
     * The sequence of native startup outcomes.
     */
    std::vector<CaptureAttempt> attempts;

    /**
     * The number of native startup attempts, including recovery attempts.
     */
    size_t starts = 0;
    std::thread::id start_thread;
    std::thread::id stop_thread;

    /**
     * The callbacks belonging to the currently open native camera.
     */
    CaptureCallbacks callbacks;
};

/**
 * Creates an independently owned, valid frame with native capture dimensions.
 */
static Frame camera_frame() {
    Frame frame;
    frame.width = 640;
    frame.height = 480;
    frame.timestamp_us = 1;
    frame.strides[0] = 2560;
    frame.planes[0].resize(2560 * 480);
    return frame;
}

/**
 * Executes scripted native capture outcomes and drains its asynchronous failure thread on stop.
 */
class ScriptedCamera final : public CameraCapture {

    /**
     * The script and active callbacks shared with the test.
     */
    std::shared_ptr<CaptureScript> script_;

    /**
     * The worker delivering a native startup failure.
     */
    std::thread worker_;

  public:

    /**
     * Retains the capture script for this native camera instance.
     */
    explicit ScriptedCamera(
        std::shared_ptr<CaptureScript> script
    )
        : script_(std::move(script)) {
    }

    /**
     * Drains native callbacks before releasing the camera instance.
     */
    ~ScriptedCamera() override {
        stop();
    }

    /**
     * Returns the scripted camera without accessing native devices.
     */
    std::vector<CameraDevice> devices() override {
        return {{"scripted", "Scripted camera", true}};
    }

    /**
     * Runs the next capture attempt, preserving asynchronous failure delivery when requested.
     *
     * @param[in] device The unused native camera identifier.
     * @param[in] format The unused encoding target; output frames retain native dimensions.
     * @param[in] callbacks The frame and failure receivers for this capture attempt.
     * @param[in] cancel The unused predicate; pending startup is cancelled by CameraController.
     */
    void start(
        const std::string& device,
        xmax_video_format format,
        CaptureCallbacks callbacks,
        const Cancel& cancel
    ) override {
        (void)device;
        (void)format;
        (void)cancel;
        CaptureAttempt attempt;
        size_t index;
        {
            std::lock_guard<std::mutex> lock(script_->mutex);
            index = script_->starts++;
            script_->start_thread = std::this_thread::get_id();
            attempt = script_->attempts.at(index);
            script_->callbacks = callbacks;
        }
        script_->changed.notify_all();

        const Error failure(
            XMAX_CAMERA_PERMISSION_DENIED,
            "Capture attempt " + std::to_string(index) + " rejected"
        );

        switch (attempt) {
        case CaptureAttempt::Ready:
            callbacks.frame(camera_frame());
            break;
        case CaptureAttempt::Throw:
            throw failure;
        case CaptureAttempt::AsyncFailure:
            worker_ = std::thread([callbacks, failure] { callbacks.failure(failure); });
            break;
        case CaptureAttempt::FrameThenFailure:
            callbacks.frame(camera_frame());
            callbacks.failure(failure);
            break;
        case CaptureAttempt::InvalidFrame:
            callbacks.frame(Frame{});
            break;
        case CaptureAttempt::Wait:
            break;
        }
    }

    /**
     * Drains the native worker and releases the active callbacks.
     */
    void stop() noexcept override {
        if (worker_.joinable())
            worker_.join();

        {
            std::lock_guard<std::mutex> lock(script_->mutex);
            script_->callbacks = {};
            script_->stop_thread = std::this_thread::get_id();
        }
        script_->changed.notify_all();
    }
};

/**
 * Creates a realtime manager whose camera and RTC adapters require no hardware or network access.
 *
 * @param[in] script The ordered capture outcomes and active native callbacks.
 *
 * @return An independent realtime manager with the scripted camera factory.
 */
static std::unique_ptr<XmaxRealtimeManager> camera_manager(
    const std::shared_ptr<CaptureScript>& script
) {
    return std::make_unique<XmaxRealtimeManager>(
        std::make_shared<ApiService>(Config{}),
        [] { return std::make_shared<FakeRtc>(); },
        std::chrono::seconds(10),
        [script] { return std::make_unique<ScriptedCamera>(script); }
    );
}

/**
 * Checks that failed reconfiguration preserves the original error and restores usable preview.
 *
 * @param[in] attempt The failure path taken by the requested configuration.
 */
static void restores_previous_format(
    CaptureAttempt attempt
) {
    auto script = std::make_shared<CaptureScript>();
    script->attempts = {CaptureAttempt::Ready, attempt, CaptureAttempt::Ready};
    auto manager = camera_manager(script);
    manager->create_local_camera_stream("scripted", {1080, 1920, 24}, false);

    const auto expected = attempt == CaptureAttempt::InvalidFrame ? XMAX_MEDIA_ERROR
                                                                 : XMAX_CAMERA_PERMISSION_DENIED;
    expect_error(expected, [&] { manager->update_camera_format({1920, 1080, 30}); });

    const auto state = manager->state_info();
    const auto format = manager->camera_format();
    CHECK(state.event.state == XMAX_READY && state.reason == XMAX_REASON_NONE);
    CHECK(format.width == 1080 && format.height == 1920 && format.fps == 24);
    CHECK(script->starts == 3);
    CHECK(manager->acquire_local_frame()->width == 640);

    CaptureCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(script->mutex);
        callbacks = script->callbacks;
    }
    auto next = camera_frame();
    next.timestamp_us = 2;
    callbacks.frame(std::move(next));
    CHECK(manager->acquire_local_frame()->timestamp_us == 2);

    // A restored camera must still report runtime device failures through normal cleanup.
    callbacks.failure(Error(XMAX_MEDIA_ERROR, "Camera disconnected after recovery"));
    CHECK(manager->camera_format().width == 0);
    CHECK(manager->state_info().event.state == XMAX_IDLE);
    CHECK(!manager->acquire_local_frame());
}

/**
 * Checks that failed recovery releases preview and retains the requested configuration's error.
 */
static void failed_recovery_closes_preview() {
    auto script = std::make_shared<CaptureScript>();
    script->attempts = {CaptureAttempt::Ready, CaptureAttempt::AsyncFailure, CaptureAttempt::AsyncFailure};
    auto manager = camera_manager(script);
    manager->create_local_camera_stream("scripted", {1080, 1920, 24}, false);

    expect_error(XMAX_CAMERA_PERMISSION_DENIED, [&] {
        manager->update_camera_format({1920, 1080, 30});
    });

    const auto state = manager->state_info();
    CHECK(state.event.state == XMAX_IDLE && state.reason == XMAX_REASON_FAILURE);
    CHECK(std::string(state.error.message) == "Capture attempt 1 rejected");
    CHECK(script->starts == 3 && manager->camera_format().width == 0);
    CHECK(!manager->acquire_local_frame());
}

/**
 * Checks that initial callback failures retain their original error and do not leave media ready.
 */
static void initial_failure_closes_preview() {
    auto script = std::make_shared<CaptureScript>();
    script->attempts = {CaptureAttempt::AsyncFailure};
    auto manager = camera_manager(script);

    expect_error(XMAX_CAMERA_PERMISSION_DENIED, [&] {
        manager->create_local_camera_stream("scripted", {1080, 1920, 24}, false);
    });

    CHECK(manager->state_info().event.state == XMAX_IDLE);
    CHECK(manager->camera_format().width == 0 && script->starts == 1);
    CHECK(!manager->acquire_local_frame());
}

/**
 * Checks that explicit cancellation stops reconfiguration without reopening the previous camera.
 */
static void cancelled_update_does_not_restore() {
    auto script = std::make_shared<CaptureScript>();
    script->attempts = {CaptureAttempt::Ready, CaptureAttempt::Wait};
    auto manager = camera_manager(script);
    manager->create_local_camera_stream("scripted", {1080, 1920, 24}, false);

    auto update = std::async(std::launch::async, [&] {
        manager->update_camera_format({1920, 1080, 30});
    });
    bool started;
    {
        std::unique_lock<std::mutex> lock(script->mutex);
        started = script->changed.wait_for(lock, std::chrono::seconds(3), [&] {
            return script->starts == 2;
        });
    }
    manager->disconnect(true);

    CHECK(started);
    expect_error(XMAX_CANCELLED, [&] { update.get(); });
    CHECK(manager->state_info().event.state == XMAX_IDLE);
    CHECK(manager->camera_format().width == 0 && script->starts == 2);
    CHECK(!manager->acquire_local_frame());
}

static void close_releases_camera_during_remote_cleanup() {
    struct BlockingCloseHttp : FakeHttp {
        std::promise<void> entered;
        std::promise<void> release;

        HttpResponse request(const HttpRequest& input, const Cancel& cancel) override {
            if (input.method == "DELETE") {
                entered.set_value();
                release.get_future().wait_for(std::chrono::seconds(5));
            }
            return FakeHttp::request(input, cancel);
        }
    };

    auto http = std::make_shared<BlockingCloseHttp>();
    auto entered = http->entered.get_future();
    auto script = std::make_shared<CaptureScript>();
    script->attempts = {CaptureAttempt::Ready};
    Config config;
    config.key = "offline-test";
    XmaxRealtimeManager manager(
        std::make_shared<ApiService>(config, http),
        [] { return std::make_shared<FakeRtc>(); },
        std::chrono::seconds(10),
        [script] { return std::make_unique<ScriptedCamera>(script); }
    );
    manager.create_local_camera_stream("scripted", {1080, 1920, 24}, false);
    manager.connect(false);
    auto disconnect = std::async(std::launch::async, [&] { manager.disconnect(false); });
    CHECK(entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    {
        std::lock_guard<std::mutex> lock(script->mutex);
        CHECK(script->callbacks.frame);
    }
    auto close = std::async(std::launch::async, [&] { manager.disconnect(true); });
    bool capture_released;
    {
        std::unique_lock<std::mutex> lock(script->mutex);
        capture_released = script->changed.wait_for(lock, std::chrono::seconds(2), [&] {
            return !script->callbacks.frame && script->stop_thread == script->start_thread;
        });
    }
    const auto closing = manager.state_info();
    http->release.set_value();
    disconnect.get();
    close.get();
    CHECK(capture_released && closing.event.state == XMAX_DISCONNECTING && closing.reason == XMAX_REASON_NONE);
    CHECK(manager.state_info().event.state == XMAX_IDLE);
}

/**
 * Runs deterministic capture recovery, runtime failure, and cancellation contracts.
 *
 * @return Zero when all recovery contracts pass; otherwise one.
 */
int main() {
    try {
        restores_previous_format(CaptureAttempt::Throw);
        restores_previous_format(CaptureAttempt::AsyncFailure);
        restores_previous_format(CaptureAttempt::FrameThenFailure);
        restores_previous_format(CaptureAttempt::InvalidFrame);
        failed_recovery_closes_preview();
        initial_failure_closes_preview();
        cancelled_update_does_not_restore();
        close_releases_camera_during_remote_cleanup();
        std::cout << "Camera recovery, original errors, runtime failure, and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
