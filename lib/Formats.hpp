#pragma once
#include <tiny_imageformat/tinyimageformat.h>
#include <vulkan/vulkan.h>

namespace ren {

auto getVkImageAspectFlags(TinyImageFormat format) -> VkImageAspectFlags;

} // namespace ren
