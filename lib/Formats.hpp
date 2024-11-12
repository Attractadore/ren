#pragma once
#include "ren/tiny_imageformat.h"

#include <boost/predef/compiler.h>
#include <vulkan/vulkan.h>

namespace ren {

constexpr TinyImageFormat HDR_FORMAT = TinyImageFormat_R16G16B16A16_SFLOAT;
constexpr TinyImageFormat SDR_FORMAT = TinyImageFormat_R8G8B8A8_UNORM;
constexpr TinyImageFormat DEPTH_FORMAT = TinyImageFormat_D32_SFLOAT;

auto getVkImageAspectFlags(TinyImageFormat format) -> VkImageAspectFlags;

} // namespace ren
