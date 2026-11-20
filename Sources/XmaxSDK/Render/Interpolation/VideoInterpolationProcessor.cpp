//
//  VideoInterpolationProcessor.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Interpolation/VideoInterpolationProcessor.hpp"
#include "Foundation/Media/FrameConverter.hpp"
#include "Render/Video/ResultVideoColor.hpp"
#include <algorithm>
#ifdef XMAX_WITH_RIFE
#include "rife.h"
#include "Render/Interpolation/RifeVulkanTransport.hpp"
#include "gpu.h"
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xmax::detail {
namespace {
struct VulkanRuntime {
    VulkanRuntime() { ncnn::create_gpu_instance(); }
    ~VulkanRuntime() { ncnn::destroy_gpu_instance(); }
};

std::filesystem::path default_model_directory() {
#ifdef _WIN32
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&make_video_interpolation_processor), &module), XMAX_MEDIA_ERROR,
        "Cannot locate interpolation runtime");
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(size && size < path.size(), XMAX_MEDIA_ERROR, "Cannot locate interpolation runtime");
    path.resize(size);
    return std::filesystem::path(path).parent_path() / "models" / "rife-v4.6";
#else
    Dl_info info{};
    require(dladdr(reinterpret_cast<const void*>(&make_video_interpolation_processor), &info) && info.dli_fname,
        XMAX_MEDIA_ERROR, "Cannot locate interpolation runtime");
    return std::filesystem::path(info.dli_fname).parent_path() / "models" / "rife-v4.6";
#endif
}

class RifeProcessor final : public VideoInterpolationProcessor {
    struct Input {
        std::shared_ptr<const Frame> source, pixels;
        std::vector<uint8_t> packed;
    };
    std::unique_ptr<RIFE> network_;
    std::unique_ptr<RifeVulkanTransport> gpu_;
    std::optional<Input> cached_;
    uint64_t prepared_frames_ = 0;

    Input prepare(std::shared_ptr<const Frame> source) {
        validate_frame(source->view());
        Input input{source, source, {}};
        if (source->format != XMAX_BGRA || !result_video_color_is_native(*source)) {
            auto converted = std::make_shared<Frame>(bgra_frame(*source));
            converted->color_primaries = source->color_primaries;
            converted->color_transfer = source->color_transfer;
            require(prepare_result_video_color(*converted), XMAX_MEDIA_ERROR,
                "Interpolation requires supported SDR video");
            input.pixels = std::move(converted);
        }
        ++prepared_frames_;
        return input;
    }

    static ncnn::Mat packed(Input& input) {
        const auto& frame = *input.pixels;
        if (input.packed.empty()) {
            input.packed.resize(size_t(frame.width) * frame.height * 3);
            for (uint32_t y = 0; y < frame.height; ++y) {
                const auto row = frame.planes[0].data() + size_t(y) * frame.strides[0];
                auto output = input.packed.data() + size_t(y) * frame.width * 3;
                for (uint32_t x = 0; x < frame.width; ++x) {
                    for (size_t c = 0; c < 3; ++c) {
#ifdef _WIN32
                        const auto channel = c;
#else
                        const auto channel = 2 - c;
#endif
                        output[x * 3 + c] = row[x * 4 + channel];
                    }
                }
            }
        }
        return ncnn::Mat(frame.width, frame.height, input.packed.data(), size_t(3), 3);
    }
  public:
    explicit RifeProcessor(const InterpolationOptions& options) {
        if (options.device_index >= 0) {
            static VulkanRuntime runtime;
            require(options.device_index < ncnn::get_gpu_count(), XMAX_UNSUPPORTED,
                "No Vulkan GPU is available for interpolation on the selected device");
        }
        const auto model = options.model_directory.empty() ? default_model_directory()
            : std::filesystem::u8path(options.model_directory);
        for (const auto name : {"flownet.param", "flownet.bin"})
            require(std::filesystem::is_regular_file(model / name) && std::filesystem::file_size(model / name) != 0,
                    XMAX_INVALID_ARGUMENT, "Missing RIFE v4.6 model files in the interpolation directory");
        if (options.device_index >= 0) {
            gpu_ = std::make_unique<RifeVulkanTransport>(options, model);
            return;
        }
        network_ = std::make_unique<RIFE>(-1, false, false, false, options.cpu_threads, false, true);
#ifdef _WIN32
        const auto result = network_->load(model.wstring());
#else
        const auto result = network_->load(model.string());
#endif
        require(result == 0, XMAX_MEDIA_ERROR, "Cannot initialize the RIFE v4.6 interpolation model");
    }

    void reset() override {
        cached_.reset();
        if (gpu_) gpu_->reset();
    }

    InterpolationTransferStats transfers() const override {
        auto stats = gpu_ ? gpu_->transfers() : InterpolationTransferStats{};
        stats.prepared_frames = prepared_frames_;
        return stats;
    }

    std::shared_ptr<Frame> midpoint(std::shared_ptr<const Frame> previous,
                                    std::shared_ptr<const Frame> next) override {
        require(previous && next && previous->width == next->width && previous->height == next->height,
            XMAX_INVALID_ARGUMENT, "Interpolation frames must have matching dimensions");
        auto left = cached_ && cached_->source == previous ? std::move(*cached_) : prepare(previous);
        cached_.reset();
        auto right = prepare(next);
        const auto& a = *left.pixels;
        const auto& b = *right.pixels;
        // A large sampled color change is treated as a scene cut instead of interpolating across it.
        uint64_t change = 0, samples = 0;
        const auto pixels = size_t(a.width) * a.height;
        for (size_t i = 0; i < pixels; i += std::max(size_t(1), pixels / 1024)) {
            for (size_t c = 0; c < 3; ++c) {
                const auto offset_a = (i / a.width) * a.strides[0] + (i % a.width) * 4 + c;
                const auto offset_b = (i / b.width) * b.strides[0] + (i % b.width) * 4 + c;
                change += std::abs(int(a.planes[0][offset_a]) - int(b.planes[0][offset_b]));
                ++samples;
            }
        }
        if (change > samples * 60) {
            reset();
            return {};
        }
        auto result = std::make_shared<Frame>();
        result->width = a.width; result->height = a.height;
        result->timestamp_us = previous->timestamp_us + (next->timestamp_us - previous->timestamp_us) / 2;
        result->presentation_mirrored = next->presentation_mirrored;
        result->color_primaries = a.color_primaries; result->color_transfer = a.color_transfer;
        result->strides[0] = size_t(a.width) * 4;
        result->planes[0].resize(pixels * 4);
        if (gpu_) {
            gpu_->midpoint(left.pixels, right.pixels, *result);
            cached_ = std::move(right);
            return result;
        }
        auto input0 = packed(left);
        auto input1 = packed(right);
        ncnn::Mat output(a.width, a.height, size_t(3), 3);
        require(!output.empty() && network_->process(input0, input1, .5f, output) == 0 && !output.empty(),
            XMAX_MEDIA_ERROR, "RIFE frame interpolation failed");
        const auto packed = static_cast<const uint8_t*>(output.data);
        for (size_t i = 0; i < pixels; ++i) {
            for (size_t c = 0; c < 3; ++c) {
#ifdef _WIN32
                const auto channel = c;
#else
                const auto channel = 2 - c;
#endif
                result->planes[0][i * 4 + c] = packed[i * 3 + channel];
            }
            result->planes[0][i * 4 + 3] = 255;
        }
        cached_ = std::move(right);
        return result;
    }
};
}
std::unique_ptr<VideoInterpolationProcessor> make_video_interpolation_processor(const InterpolationOptions& options) {
    validate_interpolation(options);
    return std::make_unique<RifeProcessor>(options);
}
} // namespace xmax::detail
#else
namespace xmax::detail {
std::unique_ptr<VideoInterpolationProcessor> make_video_interpolation_processor(const InterpolationOptions&) {
    throw Error(XMAX_UNSUPPORTED, "This SDK was built without the RIFE interpolation backend");
}
} // namespace xmax::detail
#endif
