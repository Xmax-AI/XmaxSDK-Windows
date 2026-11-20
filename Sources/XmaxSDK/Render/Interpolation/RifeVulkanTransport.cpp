//
//  RifeVulkanTransport.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Interpolation/RifeVulkanTransport.hpp"
#include "rife_ops.h"
#include "command.h"
#include "gpu.h"
#include "net.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace xmax::detail {
namespace {

// The model's RGB planes are normalized and zero-padded to multiples of 32, as in
// the pinned RIFE implementation. Packed BGRA needs no 8-bit storage extension.
constexpr char unpack_bgra[] = R"(
#version 450
#if NCNN_fp16_storage
#extension GL_EXT_shader_16bit_storage: require
#endif
layout(binding = 0) readonly buffer pixels { uint bgra[]; };
layout(binding = 1) writeonly buffer image { sfp rgb[]; };
layout(push_constant) uniform parameter { int w; int h; int stride; int outw; int outh; int cstep; } p;
void main() {
    int x = int(gl_GlobalInvocationID.x), y = int(gl_GlobalInvocationID.y), c = int(gl_GlobalInvocationID.z);
    if (x >= p.outw || y >= p.outh || c >= 3) return;
    float v = 0.f;
    if (x < p.w && y < p.h) v = float((bgra[y * p.stride + x] >> ((2 - c) * 8)) & 255u) * (1.f / 255.f);
    rgb[c * p.cstep + y * p.outw + x] = sfp(v);
})";

constexpr char fill_timestep[] = R"(
#version 450
#if NCNN_fp16_storage
#extension GL_EXT_shader_16bit_storage: require
#endif
layout(binding = 0) writeonly buffer timestep { sfp values[]; };
layout(push_constant) uniform parameter { int w; int h; } p;
void main() {
    int x = int(gl_GlobalInvocationID.x), y = int(gl_GlobalInvocationID.y);
    if (x < p.w && y < p.h) values[y * p.w + x] = sfp(.5f);
})";

constexpr char pack_bgra[] = R"(
#version 450
#if NCNN_fp16_storage
#extension GL_EXT_shader_16bit_storage: require
#endif
layout(binding = 0) readonly buffer image { sfp rgb[]; };
layout(binding = 1) writeonly buffer pixels { uint bgra[]; };
layout(push_constant) uniform parameter { int w; int h; int cstep; int outw; int outh; } p;
uint byte_value(int offset) { return uint(clamp(floor(float(rgb[offset]) * 255.f + .5f), 0.f, 255.f)); }
void main() {
    int x = int(gl_GlobalInvocationID.x), y = int(gl_GlobalInvocationID.y);
    if (x >= p.outw || y >= p.outh) return;
    int i = y * p.w + x;
    bgra[y * p.outw + x] = byte_value(i + 2 * p.cstep) | (byte_value(i + p.cstep) << 8) |
        (byte_value(i) << 16) | 0xff000000u;
})";

std::unique_ptr<ncnn::Pipeline> pipeline(const ncnn::VulkanDevice* device, const ncnn::Option& options,
                                      const char* shader, size_t size, int channels) {
    std::vector<uint32_t> spirv;
    require(ncnn::compile_spirv_module(shader, static_cast<int>(size - 1), options, spirv) == 0,
        XMAX_MEDIA_ERROR, "Cannot compile interpolation transfer shader");
    auto result = std::make_unique<ncnn::Pipeline>(device);
    result->set_optimal_local_size_xyz(8, 8, channels);
    require(result->create(spirv.data(), spirv.size() * sizeof(uint32_t), {}) == 0,
        XMAX_MEDIA_ERROR, "Cannot create interpolation transfer pipeline");
    return result;
}

using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
File open_model(const std::filesystem::path& path) {
#ifdef _WIN32
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"rb");
#else
    auto file = std::fopen(path.c_str(), "rb");
#endif
    require(file != nullptr, XMAX_MEDIA_ERROR, "Cannot open interpolation model");
    return File(file, &std::fclose);
}

} // namespace

struct RifeVulkanTransport::Impl {
    ncnn::VulkanDevice* device;
    // Allocators outlive the network, retained inputs, and every command submission.
    ncnn::VkBlobAllocator blob;
    ncnn::VkStagingAllocator staging;
    ncnn::Net network;
    std::unique_ptr<ncnn::Pipeline> unpack, pack, fill;
    std::shared_ptr<const Frame> cached_source;
    ncnn::VkMat cached_input, timestep;
    uint32_t width = 0, height = 0;
    InterpolationTransferStats stats;

    Impl(const InterpolationOptions& options, const std::filesystem::path& model)
        : device(ncnn::get_gpu_device(options.device_index)), blob(device), staging(device) {
        network.opt.num_threads = static_cast<int>(options.cpu_threads);
        network.opt.use_vulkan_compute = true;
        network.opt.use_fp16_packed = true;
        network.opt.use_fp16_storage = true;
        network.opt.use_fp16_arithmetic = false;
        network.opt.use_int8_storage = true;
        network.set_vulkan_device(device);
        network.register_custom_layer("rife.Warp", [](void*) -> ncnn::Layer* { return new Warp; });
        auto param = open_model(model / "flownet.param");
        auto weights = open_model(model / "flownet.bin");
        require(network.load_param(param.get()) == 0 && network.load_model(weights.get()) == 0,
            XMAX_MEDIA_ERROR, "Cannot initialize the RIFE v4.6 interpolation model");
        // load_param sanitizes fp16/int8 options against the selected device's capabilities.
        unpack = pipeline(device, network.opt, unpack_bgra, sizeof(unpack_bgra), 3);
        pack = pipeline(device, network.opt, pack_bgra, sizeof(pack_bgra), 1);
        fill = pipeline(device, network.opt, fill_timestep, sizeof(fill_timestep), 1);
    }

    ncnn::Option options() {
        auto opt = network.opt;
        opt.blob_vkallocator = &blob;
        opt.workspace_vkallocator = &blob;
        opt.staging_vkallocator = &staging;
        return opt;
    }

    ncnn::VkMat upload(const Frame& frame, ncnn::VkCompute& command, const ncnn::Option& opt) {
        // Include row padding without reading beyond the last active pixel. Non-word strides
        // use a row copy; the normal packed BGRA path borrows the existing owned frame directly.
        const auto stride = frame.strides[0];
        const auto bytes = (frame.height - 1) * stride + size_t(frame.width) * 4;
        ncnn::Mat host;
        int row_pixels;
        if (stride % 4 == 0 && bytes / 4 <= size_t(std::numeric_limits<int>::max())) {
            host = ncnn::Mat(static_cast<int>(bytes / 4), const_cast<uint8_t*>(frame.planes[0].data()), size_t(4), 1);
            row_pixels = static_cast<int>(stride / 4);
        } else {
            host.create(static_cast<int>(frame.width * frame.height), size_t(4), 1);
            require(!host.empty(), XMAX_MEDIA_ERROR, "Cannot allocate interpolation input");
            for (uint32_t y = 0; y < frame.height; ++y)
                std::memcpy(static_cast<uint8_t*>(host.data) + size_t(y) * frame.width * 4,
                    frame.planes[0].data() + size_t(y) * stride, size_t(frame.width) * 4);
            row_pixels = static_cast<int>(frame.width);
        }
        ncnn::VkMat packed;
        command.record_clone(host, packed, opt);
        require(!packed.empty(), XMAX_MEDIA_ERROR, "Cannot upload interpolation input");
        ncnn::VkMat input;
        input.create((frame.width + 31) / 32 * 32, (frame.height + 31) / 32 * 32, 3,
            opt.use_fp16_storage ? size_t(2) : size_t(4), 1, &blob);
        require(!input.empty(), XMAX_MEDIA_ERROR, "Cannot allocate interpolation tensor");
        std::vector<ncnn::vk_constant_type> constants(6);
        constants[0].i = frame.width; constants[1].i = frame.height; constants[2].i = row_pixels;
        constants[3].i = input.w; constants[4].i = input.h; constants[5].i = static_cast<int>(input.cstep);
        command.record_pipeline(unpack.get(), {packed, input}, constants, input);
        ++stats.uploaded_frames;
        return input;
    }

    void reset() {
        cached_source.reset(); cached_input.release(); timestep.release();
        width = height = 0;
        // A binding or geometry reset also releases cached high-resolution allocations.
        blob.clear(); staging.clear();
    }

    void midpoint(std::shared_ptr<const Frame> previous, std::shared_ptr<const Frame> next, Frame& output) {
        if (width != next->width || height != next->height) reset();
        width = next->width; height = next->height;
        const auto opt = options();
        ncnn::VkCompute command(device);
        auto left = cached_source == previous ? cached_input : upload(*previous, command, opt);
        auto right = upload(*next, command, opt);
        if (timestep.empty()) {
            timestep.create(right.w, right.h, 1, opt.use_fp16_storage ? size_t(2) : size_t(4), 1, &blob);
            require(!timestep.empty(), XMAX_MEDIA_ERROR, "Cannot allocate interpolation timestep");
            std::vector<ncnn::vk_constant_type> constants(2);
            constants[0].i = right.w; constants[1].i = right.h;
            command.record_pipeline(fill.get(), {timestep}, constants, timestep);
        }
        ncnn::VkMat interpolated;
        auto extractor = network.create_extractor();
        extractor.set_blob_vkallocator(&blob);
        extractor.set_workspace_vkallocator(&blob);
        extractor.set_staging_vkallocator(&staging);
        require(extractor.input("in0", left) == 0 && extractor.input("in1", right) == 0 &&
            extractor.input("in2", timestep) == 0 && extractor.extract("out0", interpolated, command) == 0 &&
            !interpolated.empty(), XMAX_MEDIA_ERROR, "RIFE frame interpolation failed");
        ncnn::VkMat packed;
        packed.create(static_cast<int>(width * height), size_t(4), 1, &blob);
        require(!packed.empty(), XMAX_MEDIA_ERROR, "Cannot allocate interpolation output");
        std::vector<ncnn::vk_constant_type> constants(5);
        constants[0].i = interpolated.w; constants[1].i = interpolated.h;
        constants[2].i = static_cast<int>(interpolated.cstep);
        constants[3].i = width; constants[4].i = height;
        ncnn::VkMat dispatcher;
        dispatcher.w = width; dispatcher.h = height; dispatcher.c = 1;
        command.record_pipeline(pack.get(), {interpolated, packed}, constants, dispatcher);
        ncnn::Mat host(static_cast<int>(width * height), output.planes[0].data(), size_t(4), 1);
        command.record_clone(packed, host, opt);
        require(command.submit_and_wait() == 0, XMAX_MEDIA_ERROR, "Interpolation GPU submission failed");
        cached_source = std::move(next);
        cached_input = std::move(right);
        ++stats.downloaded_frames;
    }
};

RifeVulkanTransport::RifeVulkanTransport(const InterpolationOptions& options, const std::filesystem::path& model)
    : impl_(std::make_unique<Impl>(options, model)) {}
RifeVulkanTransport::~RifeVulkanTransport() = default;
void RifeVulkanTransport::midpoint(std::shared_ptr<const Frame> previous, std::shared_ptr<const Frame> next, Frame& output) {
    impl_->midpoint(std::move(previous), std::move(next), output);
}
void RifeVulkanTransport::reset() { impl_->reset(); }
InterpolationTransferStats RifeVulkanTransport::transfers() const { return impl_->stats; }

} // namespace xmax::detail
