#include "Foundation/Media/FrameConverter.hpp"
#include "Media/MediaController.hpp"
#include "Service/Media/MediaService.hpp"
#include "TestSupport.hpp"
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
 * 摄像头帧的尺寸、像素格式、步幅和像素地址均保持不变；编码目标独立保存。
 */
static void camera_passthrough(xmax_pixel_format pixel_format) {
    auto probe = std::make_shared<CaptureProbe>();
    probe->first = native_frame(1280, 720, pixel_format, 1);
    const auto* first_pixels = probe->first.planes[0].data();
    const auto first_stride = probe->first.strides[0];
    MediaController media([probe] { return std::make_unique<ProbeCamera>(probe); });
    Frame preview;
    media.external().preview([&](Frame frame) { preview = std::move(frame); });
    media.prepare_camera("native", {1080, 1920, 30}, [](Error error) { throw error; }, [] { return false; });

    CHECK(preview.width == 1280 && preview.height == 720);
    CHECK(preview.format == pixel_format && preview.timestamp_us == 1);
    CHECK(preview.planes[0].data() == first_pixels && preview.strides[0] == first_stride);
    CHECK(media.external().format().width == 1080 && media.external().format().height == 1920);

    const uint8_t* published = nullptr;
    media.external().start([&](const Frame& frame) {
        CHECK(frame.width == 640 && frame.height == 480 && frame.format == pixel_format);
        published = frame.planes[0].data();
    });
    auto next = native_frame(640, 480, pixel_format, 2);
    const auto* next_pixels = next.planes[0].data();
    probe->callbacks.frame(std::move(next));
    CHECK(published == next_pixels && preview.planes[0].data() == next_pixels);
    CHECK(preview.width == 640 && preview.height == 480);

    media.close();
    media.prepare_external({1080, 1920, 30});
    const auto mismatched = native_frame(640, 480, pixel_format, 3);
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

int main() {
    try {
        model_formats();
        camera_passthrough(XMAX_BGRA);
        camera_passthrough(XMAX_NV12);
        camera_passthrough(XMAX_I420);
        render_conversion();
        std::cout << "Native frame ownership, dimensions, pixel formats and model presets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
