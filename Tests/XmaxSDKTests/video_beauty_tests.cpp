//
//  video_beauty_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Beauty/RemoteVideoBeautyPipeline.hpp"
#include "Render/RenderController.hpp"
#include "TestSupport.hpp"
#include <iostream>
#include <limits>
using namespace std::chrono_literals;

static std::shared_ptr<Frame> frame(int64_t timestamp = 1, uint32_t width = 4, uint32_t height = 4) {
    auto result = std::make_shared<Frame>(); result->width = width; result->height = height;
    result->timestamp_us = timestamp; result->strides[0] = width * 4;
    result->planes[0].assign(size_t(width) * height * 4, 33); return result;
}
template<class F> static void eventually(F condition) {
    const auto until = Clock::now() + 5s;
    while (!condition()) { CHECK(Clock::now() < until); std::this_thread::sleep_for(1ms); }
}
struct Stub : VideoBeautyProcessor {
    std::atomic<int>& calls; bool fails, pressure;
    Stub(std::atomic<int>& value, bool fail = false, bool drop = false) : calls(value), fails(fail), pressure(drop) {}
    void update(BeautyOptions) override {}
    std::shared_ptr<Frame> process(const Frame& input) override {
        int call = ++calls;
        if (fails) throw Error(XMAX_MEDIA_ERROR, "Synthetic beauty failure");
        if (pressure && call == 1) return {};
        auto result = std::make_shared<Frame>(input); result->planes[0][0] = 77; return result;
    }
};
static void contracts() {
    std::atomic<int> calls{0}, outputs{0}, failures{0}, created{0};
    std::shared_ptr<Frame> delivered;
    RemoteVideoBeautyPipeline pipeline([&](auto result, auto token) {
        CHECK(token == 1); delivered = result; ++outputs;
    }, [&](auto options) {
        CHECK(options.smoothness == 0 && options.lightening == 0 && options.sharpness == 0 && options.redness == 0);
        ++created; return std::make_unique<Stub>(calls);
    });
    pipeline.listener([&](auto) { ++failures; }); pipeline.bind(1, true);
    auto raw = frame(); pipeline.submit(raw, 1); eventually([&] { return outputs == 1; });
    CHECK(delivered == raw && created == 0 && calls == 0);
    pipeline.configure(BeautyOptions{sizeof(BeautyOptions), 0, 0, 0, 0});
    pipeline.submit(raw, 1); eventually([&] { return outputs == 2; });
    CHECK(delivered->timestamp_us == 1 && delivered->planes[0][0] == 77 && raw->planes[0][0] == 33);
    for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        expect_error(XMAX_INVALID_ARGUMENT, [&] { pipeline.configure(BeautyOptions{sizeof(BeautyOptions), invalid, 0, 0, 0}); });
    }
    CHECK(created == 1);
    pipeline.configure(std::nullopt); pipeline.submit(raw, 1); eventually([&] { return outputs == 3; });
    CHECK(delivered == raw && calls == 1);
    pipeline.reset(); pipeline.submit(frame(2), 1); eventually([&] { return outputs == 4; });
    CHECK(created == 1 && calls == 1 && failures == 0 && delivered->timestamp_us == 2);
    pipeline.bind(2, false); pipeline.submit(raw, 1);
    pipeline.shutdown(); CHECK(outputs == 4);
}
static void fallback_and_pressure() {
    for (bool fails : {true, false}) {
        std::atomic<int> calls{0}, outputs{0}, errors{0}; std::shared_ptr<Frame> delivered;
        RemoteVideoBeautyPipeline pipeline([&](auto result, auto) { delivered = result; ++outputs; },
            [&](auto) { return std::make_unique<Stub>(calls, fails, !fails); });
        pipeline.listener([&](auto error) { CHECK(error.code == XMAX_MEDIA_ERROR); ++errors; }); pipeline.bind(1, true);
        pipeline.configure(BeautyOptions{sizeof(BeautyOptions), 0, 0, 0, 0});
        auto raw = frame(); pipeline.submit(raw, 1); eventually([&] { return calls >= 1; });
        if (fails) {
            eventually([&] { return outputs == 1; }); CHECK(delivered == raw && errors == 1);
        }
        pipeline.submit(frame(2), 1); eventually([&] { return outputs == (fails ? 2 : 1); });
        CHECK(calls == (fails ? 1 : 2) && errors == (fails ? 1 : 0));
        pipeline.shutdown();
    }
}
static void shared_preview_and_media() {
    std::atomic<int> calls{0}, callbacks{0}; std::shared_ptr<const Frame> media;
    RenderController render([&](auto) { return std::make_unique<Stub>(calls); });
    render.media_listener([&](auto packet) { media = packet->video; ++callbacks; });
    render.beauty(BeautyOptions{sizeof(BeautyOptions), 0, 0, 0, 0});
    render.bind(1); render.receive(*frame(123), 1);
    eventually([&] { return callbacks == 1; });
    auto preview = render.acquire_frame();
    CHECK(preview && preview == media && calls == 1 && preview->timestamp_us == 123 && preview->planes[0][0] == 77);
    render.shutdown_output();
}
static void stale_in_flight() {
    struct Blocking : VideoBeautyProcessor {
        std::promise<void>& entered; std::shared_future<void> release;
        Blocking(std::promise<void>& started, std::shared_future<void> resume) : entered(started), release(resume) {}
        void update(BeautyOptions) override {}
        std::shared_ptr<Frame> process(const Frame& value) override { entered.set_value(); release.wait(); return std::make_shared<Frame>(value); }
    };
    std::promise<void> entered, release; auto resumed = release.get_future().share(); std::atomic<int> outputs{0};
    RemoteVideoBeautyPipeline pipeline([&](auto, auto) { ++outputs; }, [&](auto) { return std::make_unique<Blocking>(entered, resumed); });
    pipeline.configure(BeautyOptions{sizeof(BeautyOptions), 0, 0, 0, 0});
    pipeline.bind(1, true); pipeline.submit(frame(), 1); entered.get_future().wait();
    pipeline.bind(2, true); release.set_value(); pipeline.configure(std::nullopt);
    CHECK(outputs == 0); pipeline.submit(frame(2), 2); eventually([&] { return outputs == 1; });
}
static void shared_sdr_preparation_and_hdr_fallback() {
    std::atomic<int> calls{0}, outputs{0}, errors{0};
    std::shared_ptr<Frame> delivered;
    RemoteVideoBeautyPipeline pipeline([&](auto result, auto) {
        delivered = std::move(result);
        ++outputs;
    }, [&](auto) { return std::make_unique<Stub>(calls); });
    pipeline.listener([&](auto) { ++errors; });
    pipeline.bind(1, true);

    auto source = frame();
    source->color_primaries = 9; // BT.2020 SDR
    source->color_transfer = 1;
    source->planes[0][1] = 116;
    pipeline.submit(std::make_shared<Frame>(*source), 1);
    eventually([&] { return outputs == 1; });
    const auto prepared = delivered->planes[0];
    CHECK(delivered->color_primaries == 1 && prepared != source->planes[0]);

    pipeline.configure(BeautyOptions{sizeof(BeautyOptions), 0, 0, 0, 0});
    pipeline.submit(std::make_shared<Frame>(*source), 1);
    eventually([&] { return outputs == 2; });
    CHECK(calls == 1 && delivered->planes[0][0] == 77);
    CHECK(std::equal(prepared.begin() + 1, prepared.end(), delivered->planes[0].begin() + 1));

    source->color_transfer = 16; // PQ HDR cannot enter the SDR effect.
    pipeline.submit(source, 1);
    eventually([&] { return outputs == 3; });
    CHECK(delivered == source && calls == 1 && errors == 1);
}

static void real_worker() {
    auto raw = frame(456, 1920, 1080);
    for (size_t i = 0; i < raw->planes[0].size(); i += 4) {
        int noise = (int((i / 4) % 7) - 3) * 5;
        raw->planes[0][i] = uint8_t(90 + noise); raw->planes[0][i + 1] = uint8_t(120 + noise);
        raw->planes[0][i + 2] = uint8_t(160 + noise); raw->planes[0][i + 3] = 255;
    }
    auto original = raw->planes[0];
    auto plainWorker = make_video_beauty_processor({sizeof(BeautyOptions), 0, 0, 0, 0}); auto plain = plainWorker->process(*raw);
    plainWorker->update({sizeof(BeautyOptions), 1, 1, 0, 0}); auto strong = plainWorker->process(*raw);
    double delta = 0, neutral_delta = 0;
    for (size_t i = 0; i < original.size(); ++i) {
        if (i % 4 == 3)
            continue;
        delta += std::abs(int(plain->planes[0][i]) - int(strong->planes[0][i]));
        neutral_delta += std::abs(int(plain->planes[0][i]) - int(original[i]));
    }
    delta /= 1920 * 1080 * 3;
    neutral_delta /= 1920 * 1080 * 3;
    std::cout << "ClearVision 1080p mean RGB difference with zero strengths: " << neutral_delta << '\n';
    CHECK(neutral_delta < 8);
    CHECK(delta > 1 && strong->timestamp_us == 456 && strong->width == 1920 && strong->height == 1080 && raw->planes[0] == original);
    std::cout << "PASS actual SDK ClearVision 1080p, mean RGB difference " << delta << '\n';
}
int main(int argc, char**) {
    try {
        contracts(); fallback_and_pressure(); shared_preview_and_media(); stale_in_flight();
        shared_sdr_preparation_and_hdr_fallback();
        if (argc > 1) real_worker();
        std::cout << "PASS SDK beauty defaults, validation, fallback, capacity, shared output and stale-frame isolation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
