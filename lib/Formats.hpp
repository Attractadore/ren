#pragma once
#include "core/StdDef.hpp"
#include "ren/tiny_imageformat.h"

#include <boost/predef/compiler.h>
#include <vulkan/vulkan.h>

namespace ren {

constexpr usize FORMAT_BITS = 8;
static_assert(TinyImageFormat_Count <= (1 << FORMAT_BITS));

constexpr TinyImageFormat HDR_FORMAT = TinyImageFormat_R16G16B16A16_SFLOAT;
constexpr TinyImageFormat SDR_FORMAT = TinyImageFormat_R8G8B8A8_UNORM;
constexpr TinyImageFormat DEPTH_FORMAT = TinyImageFormat_D32_SFLOAT;

auto getVkImageAspectFlags(TinyImageFormat format) -> VkImageAspectFlags;

} // namespace ren
