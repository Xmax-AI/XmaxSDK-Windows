//
//  frame_conversion_tests.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/FrameConverter.hpp"
#include "Media/Video/VideoFrameScaler.hpp"
#include "Media/MediaController.hpp"
#include "Service/Media/MediaService.hpp"
#include "TestSupport.hpp"
#include <array>
#include <iostream>

/**
 * 构造具有独立存储的测试帧，保留像素格式和字节步幅。
 */
static Frame native_frame(uint32_t width, uint32_t height, xmax_pixel_format format, int64_t timestamp) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.format = format;
    frame.timestamp_us = timestamp;
    const int planes = format == XMAX_BGRA ? 1 : (format == XMAX_NV12 ? 2 : 3);

    for (int p = 0; p < planes; ++p) {
        const auto rows = p == 0 ? height : height / 2;
        frame.strides[p] =
            format == XMAX_BGRA ? width * 4 : (p == 0 || format == XMAX_NV12 ? width : width / 2);
        frame.planes[p].assign(frame.strides[p] * rows, p == 0 ? 235 : 128);
    }

    return frame;
}

/**
 * 将测试帧按原生采集回调交付，避免依赖硬件或网络。
 */
struct CaptureProbe {

    Frame first;

    CaptureCallbacks callbacks;
};

struct ProbeCamera final : CameraCapture {
    std::shared_ptr<CaptureProbe> probe;

    explicit ProbeCamera(std::shared_ptr<CaptureProbe> value) : probe(std::move(value)) {
    }

    std::vector<CameraDevice> devices() override {
        return {{"native", "Native camera", true}};
    }

    void start(const std::string&, xmax_video_format, CaptureCallbacks callbacks, const Cancel&) override {
        probe->callbacks = std::move(callbacks);
        probe->callbacks.frame(std::move(probe->first));
    }

    void stop() noexcept override {
        probe->callbacks = {};
    }
};

/**
 * 横向采集帧在预览和发布前使用同一份竖向居中裁切；尺寸匹配时复用原像素。
 */
static void camera_output_format(xmax_pixel_format pixel_format) {
    auto probe = std::make_shared<CaptureProbe>();
    probe->first = native_frame(18, 32, pixel_format, 1);
    const auto* first_pixels = probe->first.planes[0].data();
    const auto first_stride = probe->first.strides[0];
    MediaController media([probe] { return std::make_unique<ProbeCamera>(probe); });
    Frame preview;
    media.external().preview([&](Frame frame) { preview = std::move(frame); });
    media.prepare_camera("native", {18, 32, 30}, [](Error error) { throw error; }, [] { return false; });

    CHECK(preview.width == 18 && preview.height == 32);
    CHECK(preview.format == pixel_format && preview.timestamp_us == 1);
    CHECK(preview.planes[0].data() == first_pixels && preview.strides[0] == first_stride);

    const uint8_t* published = nullptr;
    media.external().start([&](const Frame& frame) {
        CHECK(frame.width == 18 && frame.height == 32 && frame.format == pixel_format);
        CHECK(frame.timestamp_us == 2 && frame.color_primaries == 1);
        published = frame.planes[0].data();
    });
    auto next = native_frame(32, 18, pixel_format, 2);
    next.color_primaries = 1;
    const auto pixel_size = pixel_format == XMAX_BGRA ? 4u : 1u;
    for (uint32_t y = 0; y < next.height; ++y)
        for (uint32_t x = 0; x < next.width; ++x)
            next.planes[0][y * next.strides[0] + x * pixel_size] = uint8_t(x * 6);
    probe->callbacks.frame(std::move(next));
    CHECK(published && preview.planes[0].data() == published);
    CHECK(preview.width == 18 && preview.height == 32 && preview.color_primaries == 1);
    const auto* center_row = preview.planes[0].data() + 16 * preview.strides[0];
    CHECK(center_row[0] >= 54 && center_row[17 * pixel_size] <= 114);
    CHECK(center_row[17 * pixel_size] > center_row[0]);

    media.close();
    media.prepare_external({18, 32, 30});
    const auto mismatched = native_frame(32, 18, pixel_format, 3);
    expect_error(XMAX_INVALID_ARGUMENT, [&] { media.external().push(mismatched.view()); });
}

/**
 * 模型尺寸用于摄像头编码和生成画布，1080p 兼容参数映射至支持的尺寸。
 */
static void model_formats() {
    for (const auto& preset : MediaService::formats) {
        const auto input = MediaService::camera_input(preset);
        const auto canvas = MediaService::generation(input);
        CHECK(input.width == preset.width && input.height == preset.height);
        CHECK(canvas.width == preset.width && canvas.height == preset.height);
    }

    CHECK(MediaService::camera_input({1920, 1080, 24}).height == 1080);
    CHECK(MediaService::camera_input({1080, 1920, 24}).width == 1080);
    expect_error(XMAX_INVALID_ARGUMENT, [] { MediaService::camera_input({1920, 1024, 24}); });
    expect_error(XMAX_INVALID_ARGUMENT, [] { MediaService::camera_input({1024, 1920, 24}); });
    CHECK(RealtimeModelDefinition::preset({1920, 1024, 24}) == XMAX_CAMERA_PRESET_NONE);
    CHECK(MediaService::camera_input({1080, 1920, 24}).fps == 24);
    CHECK(MediaService::input({1920, 1024, 30}).height == 1024);
    const auto portrait = MediaService::generation({1024, 1920, 30});
    CHECK(portrait.width == 1080 && portrait.height == 1920);
    const auto square = MediaService::generation({800, 800, 30});
    CHECK(square.width == 1408 && square.height == 1408);
    const auto landscape = MediaService::generation({640, 480, 30});
    CHECK(landscape.width == 1536 && landscape.height == 1152);
    expect_error(XMAX_INVALID_ARGUMENT, [] { MediaService::camera_input({640, 480, 30}); });
}

/**
 * 颜色转换仅供呈现使用，转换前后的宽高和时间戳保持一致。
 */
static void render_conversion() {
    for (auto format : {XMAX_NV12, XMAX_I420}) {
        auto source = native_frame(4, 2, format, 7);
        const auto rgb = bgra_frame(source);
        CHECK(rgb.width == 4 && rgb.height == 2 && rgb.timestamp_us == 7);
        CHECK(rgb.format == XMAX_BGRA && rgb.planes[0][0] >= 254 && rgb.planes[0][3] == 255);
    }
}

static void external_center_crop() {
    auto bgra = native_frame(8, 4, XMAX_BGRA, 10);
    for (size_t y = 0; y < bgra.height; ++y)
        for (size_t x = 0; x < bgra.width; ++x)
            bgra.planes[0][y * bgra.strides[0] + 4 * x] = uint8_t(20 * x);
    const auto square = copy_scaled_video_frame(bgra.view(), {4, 4, 30}, true);
    CHECK(square.timestamp_us == 10 && square.planes[0][0] == 40 && square.planes[0][12] == 100);
    const auto scaled = copy_scaled_video_frame(bgra.view(), {2, 2, 30}, true);
    CHECK(scaled.planes[0][0] > 40 && scaled.planes[0][0] < 60);
    CHECK(scaled.planes[0][4] > 80 && scaled.planes[0][4] < 100);

    auto yuv = native_frame(8, 4, XMAX_I420, 11);
    yuv.planes[1] = {20, 40, 60, 80, 20, 40, 60, 80};
    yuv.planes[2] = {25, 45, 65, 85, 25, 45, 65, 85};
    const auto nv12 = copy_scaled_video_frame(yuv.view(), {4, 4, 30}, true);
    CHECK(nv12.format == XMAX_NV12 && nv12.planes[1] == std::vector<uint8_t>({40, 45, 60, 65, 40, 45, 60, 65}));
}

static void corrects_camera_pixel_orientation() {
    auto frame = native_frame(4, 2, XMAX_BGRA, 123);
    for (uint8_t i = 0; i < 8; ++i)
        frame.planes[0][i * 4] = i;
    const auto rotated = rotate_bgra(std::move(frame), 90);
    CHECK(rotated.width == 2 && rotated.height == 4 && rotated.timestamp_us == 123);
    const std::array<uint8_t, 8> expected{4, 0, 5, 1, 6, 2, 7, 3};
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK(rotated.planes[0][i * 4] == expected[i]);
}

int main() {
    try {
        corrects_camera_pixel_orientation();
        model_formats();
        camera_output_format(XMAX_BGRA);
        camera_output_format(XMAX_NV12);
        camera_output_format(XMAX_I420);
        render_conversion();
        external_center_crop();
        std::cout << "Native frame ownership, dimensions, pixel formats and model presets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
