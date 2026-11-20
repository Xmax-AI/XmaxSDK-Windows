//
//  RifeVulkanTransport.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Render/Interpolation/VideoInterpolationProcessor.hpp"
#include <filesystem>

namespace xmax::detail {

// RIFE v4.6, with BGRA conversion on Vulkan and the last input retained on the device.
// CPU-readable BGRA output remains necessary for the existing preview and public frame API.
class RifeVulkanTransport {
  public:
    RifeVulkanTransport(const InterpolationOptions& options, const std::filesystem::path& model);
    ~RifeVulkanTransport();
    void midpoint(std::shared_ptr<const Frame> previous, std::shared_ptr<const Frame> next, Frame& output);
    void reset();
    InterpolationTransferStats transfers() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xmax::detail
