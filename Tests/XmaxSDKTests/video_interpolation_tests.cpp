//
//  video_interpolation_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Interpolation/RemoteVideoInterpolationPipeline.hpp"
#include "Render/RenderController.hpp"
#include "TestSupport.hpp"
#include <algorithm>
#include <iostream>
#ifdef XMAX_TEST_WITH_RIFE
#include "rife.h"
#include "Foundation/Media/FrameConverter.hpp"
#include "Render/Video/ResultVideoColor.hpp"
#include <filesystem>
#endif
using namespace std::chrono_literals;

static std::shared_ptr<Frame> frame(int64_t time, uint32_t width = 64) {
    auto value = std::make_shared<Frame>();
    value->width = width; value->height = 64; value->timestamp_us = time;
    value->strides[0] = width * 4;
    value->planes[0].assign(size_t(width) * 64 * 4, 127);
    for (size_t i = 3; i < value->planes[0].size(); i += 4) value->planes[0][i] = 255;
    return value;
}

template<class F> static void eventually(F condition) {
    const auto deadline = Clock::now() + 5s;
    while (!condition()) { CHECK(Clock::now() < deadline); std::this_thread::sleep_for(1ms); }
}

struct Processor : VideoInterpolationProcessor {
    bool fail = false;
    explicit Processor(bool failure = false) : fail(failure) {}
    std::shared_ptr<Frame> midpoint(std::shared_ptr<const Frame> a, std::shared_ptr<const Frame> b) override {
        if (fail) throw Error(XMAX_MEDIA_ERROR, "Synthetic interpolation failure");
        auto value = std::make_shared<Frame>(*a);
        value->timestamp_us = a->timestamp_us + (b->timestamp_us - a->timestamp_us) / 2;
        return value;
    }
};

static void timed_delivery_and_reset() {
    std::mutex mutex;
    std::vector<int64_t> timestamps;
    std::vector<Clock::time_point> times;
    RemoteVideoInterpolationPipeline pipeline([&](auto value, auto token) {
        CHECK(token == 1);
        std::lock_guard<std::mutex> lock(mutex);
        timestamps.push_back(value->timestamp_us);
        times.push_back(Clock::now());
    }, [](const auto&) { return std::make_unique<Processor>(); });
    pipeline.bind(1, true);
    // Wide timestamp spacing verifies scheduling without assuming sub-20 ms VM timers.
    pipeline.configure(InterpolationOptions{"", -1, 1, 500});
    pipeline.submit(frame(1000), 1);
    std::this_thread::sleep_for(40ms);
    pipeline.submit(frame(201000), 1);
    try {
        eventually([&] { std::lock_guard<std::mutex> lock(mutex); return timestamps.size() == 3; });
    } catch (...) {
        const auto status = pipeline.status();
        std::lock_guard<std::mutex> lock(mutex);
        std::cerr << "Timed delivery failed: enabled=" << status.enabled << " skipped=" << status.skipped_frames
            << " interpolated=" << status.interpolated_frames << " output timestamps:";
        for (auto time : timestamps) std::cerr << ' ' << time;
        std::cerr << '\n';
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        CHECK((timestamps == std::vector<int64_t>{1000, 101000, 201000}));
        CHECK(times[1] - times[0] > 5ms && times[2] - times[1] > 5ms);
    }
    CHECK(pipeline.status().input_frames == 2 && pipeline.status().interpolated_frames == 1);
    pipeline.submit(frame(401000), 1);
    pipeline.bind(2, false);
    std::this_thread::sleep_for(180ms);
    { std::lock_guard<std::mutex> lock(mutex); CHECK(timestamps.size() == 3); }
    pipeline.configure(std::nullopt);
    CHECK(!pipeline.status().enabled);
}

static void failure_and_configuration_rollback() {
    std::atomic<int> outputs{0};
    RemoteVideoInterpolationPipeline pipeline([&](auto, auto) { ++outputs; }, [](const auto& options) {
        if (options.device_index == 9) throw Error(XMAX_UNSUPPORTED, "Unavailable test device");
        return std::make_unique<Processor>(true);
    });
    pipeline.bind(1, true);
    pipeline.configure(InterpolationOptions{"", -1, 1, 125});
    expect_error(XMAX_UNSUPPORTED, [&] { pipeline.configure(InterpolationOptions{"", 9, 1, 125}); });
    CHECK(pipeline.status().enabled);
    pipeline.submit(frame(1000), 1);
    pipeline.submit(frame(41000), 1);
    eventually([&] { return !pipeline.status().enabled; });
    CHECK(pipeline.status().error_code == XMAX_MEDIA_ERROR);
    pipeline.submit(frame(81000), 1);
    eventually([&] { return outputs >= 2; });
    pipeline.configure(std::nullopt);
    CHECK(pipeline.status().error_code == XMAX_OK);
    expect_error(XMAX_INVALID_ARGUMENT, [&] { pipeline.configure(InterpolationOptions{"", -2, 1, 125}); });
}

static void stale_inference_and_slow_processing() {
    struct Blocking : Processor {
        std::promise<void>& entered;
        std::shared_future<void> resume;
        Blocking(std::promise<void>& start, std::shared_future<void> signal) : entered(start), resume(signal) {}
        std::shared_ptr<Frame> midpoint(std::shared_ptr<const Frame> a, std::shared_ptr<const Frame> b) override {
            entered.set_value(); resume.wait(); return Processor::midpoint(a, b);
        }
    };
    std::promise<void> entered, release;
    auto signal = release.get_future().share();
    std::atomic<int> outputs{0};
    RemoteVideoInterpolationPipeline pipeline([&](auto, auto) { ++outputs; }, [&](const auto&) {
        return std::make_unique<Blocking>(entered, signal);
    });
    pipeline.bind(1, true);
    pipeline.configure(InterpolationOptions{"", -1, 1, 50});
    pipeline.submit(frame(1000), 1); pipeline.submit(frame(201000), 1);
    entered.get_future().wait();
    // Original preview delivery continues while inference is blocked.
    eventually([&] { return outputs == 2; });
    pipeline.bind(2, true);
    release.set_value();
    pipeline.configure(std::nullopt);
    CHECK(outputs == 2 && pipeline.status().interpolated_frames == 0);
    pipeline.submit(frame(1000), 1);
    CHECK(outputs == 2);
    pipeline.submit(frame(1000), 2);
    CHECK(outputs == 3);
}

static void recording_cadence_is_preserved() {
    std::atomic<int> media{0};
    RenderController render(make_video_beauty_processor, [](const auto&) { return std::make_unique<Processor>(); });
    render.media_listener([&](auto) { ++media; });
    render.interpolation(InterpolationOptions{"", -1, 1, 500});
    render.bind(1);
    render.receive(*frame(1000), 1);
    eventually([&] { return media == 1; });
    std::this_thread::sleep_for(40ms);
    render.receive(*frame(201000), 1);
    eventually([&] { return render.interpolation_status().interpolated_frames == 1; });
    CHECK(media == 2);
    render.shutdown_output();
}

static void unbinding_releases_retained_input() {
    struct Retaining : Processor {
        std::shared_ptr<const Frame> retained;
        std::shared_ptr<Frame> midpoint(std::shared_ptr<const Frame> a, std::shared_ptr<const Frame> b) override {
            retained = b;
            return Processor::midpoint(std::move(a), std::move(b));
        }
        void reset() override { retained.reset(); }
    };
    RemoteVideoInterpolationPipeline pipeline([](auto, auto) {},
        [](const auto&) { return std::make_unique<Retaining>(); });
    pipeline.configure(InterpolationOptions{"", -1, 1, 500});
    pipeline.bind(1, true);
    auto a = frame(1000), b = frame(201000);
    std::weak_ptr<Frame> weak = b;
    pipeline.submit(a, 1); pipeline.submit(b, 1);
    eventually([&] { return pipeline.status().interpolated_frames == 1; });
    a.reset(); b.reset();
    CHECK(!weak.expired());
    pipeline.bind(2, false);
    eventually([&] { return weak.expired(); });
}

static void real_model(const char* directory, int device) {
    InterpolationOptions options{directory, device, 2, 500};
    auto processor = make_video_interpolation_processor(options);
    auto a = frame(1000), b = frame(201000);
    b->planes[0][0] = 120;
    const auto output = processor->midpoint(a, b);
    CHECK(output && output->width == 64 && output->height == 64 && output->timestamp_us == 101000);
    CHECK(output->planes[0].size() == 64 * 64 * 4 && output->planes[0][3] == 255);
    auto hdr = std::make_shared<Frame>(*b);
    hdr->color_transfer = 16;
    expect_error(XMAX_MEDIA_ERROR, [&] { processor->midpoint(a, hdr); });
    // Reuse the warm model inside the normal preview scheduling entry point.
    std::atomic<int> outputs{0};
    RemoteVideoInterpolationPipeline pipeline([&](auto, auto) { ++outputs; }, [&](const auto&) { return std::move(processor); });
    pipeline.configure(options); pipeline.bind(1, true);
    pipeline.submit(a, 1); pipeline.submit(b, 1);
    // Slow CPU devices may correctly miss the preview deadline; originals must still arrive.
    eventually([&] {
        const auto status = pipeline.status();
        return status.interpolated_frames + status.skipped_frames == 1 &&
            outputs == 2 + int(status.interpolated_frames);
    });
    const auto status = pipeline.status();
    CHECK(status.enabled && status.error_code == XMAX_OK);
    CHECK(outputs == 2 + int(status.interpolated_frames));
    std::cout << "Real RIFE model and scheduled midpoint passed on device " << device << '\n';
}

#ifdef XMAX_TEST_WITH_RIFE
// Preserve the original packed RGB/BGR path as a numerical and opt-in timing reference.
struct ReferenceRife {
    RIFE network;
    ReferenceRife(const char* directory, int device) : network(device, false, false, false, 2, false, true) {
#ifdef _WIN32
        CHECK(network.load(std::filesystem::u8path(directory).wstring()) == 0);
#else
        CHECK(network.load(directory) == 0);
#endif
    }
    Frame midpoint(const Frame& left, const Frame& right) {
        auto a = bgra_frame(left), b = bgra_frame(right);
        a.color_primaries = left.color_primaries; a.color_transfer = left.color_transfer;
        b.color_primaries = right.color_primaries; b.color_transfer = right.color_transfer;
        CHECK(prepare_result_video_color(a) && prepare_result_video_color(b));
        const auto pixels = size_t(a.width) * a.height;
        std::vector<uint8_t> p(pixels * 3), q(pixels * 3);
        for (uint32_t y = 0; y < a.height; ++y) {
            for (uint32_t x = 0; x < a.width; ++x) {
                for (size_t c = 0; c < 3; ++c) {
#ifdef _WIN32
                    const auto channel = c;
#else
                    const auto channel = 2 - c;
#endif
                    p[(size_t(y) * a.width + x) * 3 + c] = a.planes[0][y * a.strides[0] + x * 4 + channel];
                    q[(size_t(y) * a.width + x) * 3 + c] = b.planes[0][y * b.strides[0] + x * 4 + channel];
                }
            }
        }
        ncnn::Mat i(a.width, a.height, p.data(), size_t(3), 3);
        ncnn::Mat j(a.width, a.height, q.data(), size_t(3), 3);
        ncnn::Mat output(a.width, a.height, size_t(3), 3);
        CHECK(network.process(i, j, .5f, output) == 0);
        Frame result;
        result.width = a.width; result.height = a.height; result.strides[0] = a.width * 4;
        result.planes[0].resize(pixels * 4);
        const auto bytes = static_cast<const uint8_t*>(output.data);
        for (size_t x = 0; x < pixels; ++x) {
            for (size_t c = 0; c < 3; ++c) {
#ifdef _WIN32
                const auto channel = c;
#else
                const auto channel = 2 - c;
#endif
                result.planes[0][x * 4 + c] = bytes[x * 3 + channel];
            }
            result.planes[0][x * 4 + 3] = 255;
        }
        return result;
    }
};

static std::shared_ptr<Frame> colored_frame(int index, uint32_t width, uint32_t height, size_t padding = 0) {
    auto value = std::make_shared<Frame>();
    value->width = width; value->height = height; value->timestamp_us = 1000 + index * 40000;
    value->strides[0] = size_t(width) * 4 + padding;
    // Intentionally omit the last row's padding, as allowed by the frame ABI.
    value->planes[0].assign((height - 1) * value->strides[0] + size_t(width) * 4, 0xee);
    for (uint32_t y = 0; y < height; ++y) {
        auto row = value->planes[0].data() + y * value->strides[0];
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 4] = uint8_t(20 + (x + index * 3) % 80);
            row[x * 4 + 1] = uint8_t(70 + (y + index * 2) % 80);
            row[x * 4 + 2] = uint8_t(150 + (x + y + index) % 80);
            row[x * 4 + 3] = 87;
        }
    }
    return value;
}

static void transfer_reuse_and_reference(const char* directory, int device) {
    auto processor = make_video_interpolation_processor({directory, device, 2, 500});
    ReferenceRife reference(directory, device);
    const auto compare = [&](const auto& a, const auto& b) {
        const auto actual = processor->midpoint(a, b);
        const auto expected = reference.midpoint(*a, *b);
        CHECK(actual && actual->width == b->width && actual->height == b->height);
        CHECK(actual->timestamp_us == a->timestamp_us + (b->timestamp_us - a->timestamp_us) / 2);
        CHECK(actual->presentation_mirrored == b->presentation_mirrored);
        CHECK(actual->planes[0].size() == expected.planes[0].size());
        int maximum = 0;
        for (size_t i = 0; i < actual->planes[0].size(); ++i) {
            maximum = std::max(maximum, std::abs(int(actual->planes[0][i]) - int(expected.planes[0][i])));
            if (i % 4 == 3) CHECK(actual->planes[0][i] == 255);
        }
        CHECK(maximum <= 1);
        std::cout << "Reference comparison max channel difference: " << maximum << '\n';
    };
    auto a = colored_frame(0, 66, 70, 12), b = colored_frame(1, 66, 70, 1);
    auto c = colored_frame(2, 66, 70);
    c->presentation_mirrored = true;
    const auto original_a = a->planes[0], original_b = b->planes[0];
    compare(a, b); compare(b, c);
    CHECK(a->planes[0] == original_a && b->planes[0] == original_b);
    CHECK(processor->transfers().prepared_frames == 3);
    CHECK(processor->transfers().uploaded_frames == (device >= 0 ? 3 : 0));
    CHECK(processor->transfers().downloaded_frames == (device >= 0 ? 2 : 0));
    processor->reset();
    compare(b, c); // A new binding must re-upload both inputs.
    CHECK(processor->transfers().prepared_frames == 5);
    CHECK(processor->transfers().uploaded_frames == (device >= 0 ? 5 : 0));
    auto d = colored_frame(3, 96, 64), e = colored_frame(4, 96, 64);
    d->color_primaries = e->color_primaries = 12;
    d->color_transfer = e->color_transfer = 13;
    const auto original_d = d->planes[0];
    compare(d, e); // Resolution and SDR color conversion invalidate the previous input.
    CHECK(d->planes[0] == original_d);
    CHECK(processor->transfers().uploaded_frames == (device >= 0 ? 7 : 0));
    auto cut = colored_frame(5, 96, 64);
    std::fill(cut->planes[0].begin(), cut->planes[0].end(), 0);
    CHECK(!processor->midpoint(e, cut));
    CHECK(processor->transfers().uploaded_frames == (device >= 0 ? 7 : 0));
    compare(d, e); // A scene cut also clears retained Vulkan input.
    CHECK(processor->transfers().uploaded_frames == (device >= 0 ? 9 : 0));
    std::cout << "Frame transfer reuse, reset, stride, SDR color, and scene-cut checks passed\n";
}

static void benchmark(const char* directory, int device, uint32_t width, uint32_t height, int count) {
    CHECK(width >= 2 && width <= 1920 && height >= 2 && height <= 1920 && count >= 2 && count <= 60);
    auto processor = make_video_interpolation_processor({directory, device, 2, 500});
    ReferenceRife reference(directory, device);
    std::vector<std::shared_ptr<Frame>> frames;
    for (int i = 0; i <= count; ++i) frames.push_back(colored_frame(i, width, height));
    // Warm the model and shader pipelines before reporting timings. No timing assertion in CI.
    CHECK(processor->midpoint(frames[0], frames[1]));
    reference.midpoint(*frames[0], *frames[1]);
    processor->reset();
    const auto measure = [&](auto&& run) {
        std::vector<double> times;
        for (int i = 0; i < count; ++i) {
            const auto start = Clock::now();
            run(frames[i], frames[i + 1]);
            times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        std::sort(times.begin(), times.end());
        std::cout << "median_ms=" << times[times.size() / 2] << " max_ms=" << times.back() << '\n';
    };
    std::cout << "Reference " << width << 'x' << height << " device=" << device << " pairs=" << count << '\n';
    measure([&](auto a, auto b) { reference.midpoint(*a, *b); });
    const auto before = processor->transfers();
    std::cout << "Optimized\n";
    measure([&](auto a, auto b) { CHECK(processor->midpoint(a, b)); });
    const auto after = processor->transfers();
    CHECK(after.prepared_frames - before.prepared_frames == uint64_t(count + 1));
    CHECK(after.uploaded_frames - before.uploaded_frames == (device >= 0 ? uint64_t(count + 1) : 0));
    std::cout << "prepared=" << after.prepared_frames - before.prepared_frames
        << " uploaded=" << after.uploaded_frames - before.uploaded_frames
        << " downloaded=" << after.downloaded_frames - before.downloaded_frames << '\n';
}
#endif

int main(int argc, char** argv) {
    try {
#ifdef XMAX_TEST_WITH_RIFE
        if (argc == 7 && std::string(argv[1]) == "--benchmark") {
            benchmark(argv[2], std::stoi(argv[3]), std::stoul(argv[4]), std::stoul(argv[5]), std::stoi(argv[6]));
            return 0;
        }
#endif
        std::cout << "Checking timed delivery and reset" << std::endl;
        timed_delivery_and_reset();
        std::cout << "Checking failure and configuration rollback" << std::endl;
        failure_and_configuration_rollback();
        std::cout << "Checking stale inference and slow processing" << std::endl;
        stale_inference_and_slow_processing();
        std::cout << "Checking recording cadence" << std::endl;
        recording_cadence_is_preserved();
        std::cout << "Checking retained input release on unbind" << std::endl;
        unbinding_releases_retained_input();
        if (argc == 3) {
            std::cout << "Checking the real RIFE model" << std::endl;
            real_model(argv[1], std::stoi(argv[2]));
#ifdef XMAX_TEST_WITH_RIFE
            transfer_reuse_and_reference(argv[1], std::stoi(argv[2]));
#endif
        }
        std::cout << "Video interpolation tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
