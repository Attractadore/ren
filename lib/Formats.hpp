#pragma once
#include "Support/Flags.hpp"
#include "Support/StdDef.hpp"
#include "ren/ren.hpp"

#include <vulkan/vulkan.h>

namespace ren {

REN_BEGIN_FLAGS_ENUM(FormatProperty){
    REN_FLAG(Color),
    REN_FLAG(SRGB),
    REN_FLAG(Depth),
    REN_FLAG(Stencil),
} REN_END_FLAGS_ENUM(FormatProperty);

struct FormatProperties {
  FormatPropertyFlags flags;
  union {
    struct {
      u32 red_bits = 0;
      u32 green_bits = 0;
      u32 blue_bits = 0;
      u32 alpha_bits = 0;
    };
    struct {
      u32 depth_bits;
      u32 stencil_bits;
    };
    u32 unused_bits;
  };
};

auto getVkFormat(Format format) -> VkFormat;

auto getFormatProperties(VkFormat format) -> FormatProperties;

auto isSRGBFormat(VkFormat format) -> bool;
auto isColorFormat(VkFormat format) -> bool;
auto isDepthFormat(VkFormat format) -> bool;
auto isStencilFormat(VkFormat format) -> bool;

auto getSRGBFormat(VkFormat format) -> VkFormat;

auto get_format_size(VkFormat format) -> u32;

auto getVkImageAspectFlags(VkFormat format) -> VkImageAspectFlags;

} // namespace ren
