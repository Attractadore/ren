#pragma once
#include "Support/StdDef.hpp"

#include <vulkan/vulkan.h>

namespace ren {

constexpr usize PIPELINE_DEPTH = 2;
constexpr VkFormat COLOR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr usize MAX_COLOR_ATTACHMENTS = 8;

} // namespace ren
