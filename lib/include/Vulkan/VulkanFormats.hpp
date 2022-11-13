#pragma once
#include "Formats.inl"
#include "Support/Enum.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace ren {
namespace detail {
constexpr std::array format_map = {
    std::pair(Format::RGB8, VK_FORMAT_R8G8B8_UNORM),
    std::pair(Format::RGB8_SRGB, VK_FORMAT_R8G8B8_SRGB),
    std::pair(Format::BGR8, VK_FORMAT_B8G8R8_UNORM),
    std::pair(Format::BGR8_SRGB, VK_FORMAT_B8G8R8_SRGB),
    std::pair(Format::RGBA8, VK_FORMAT_R8G8B8A8_UNORM),
    std::pair(Format::RGBA8_SRGB, VK_FORMAT_R8G8B8A8_SRGB),
    std::pair(Format::BGRA8, VK_FORMAT_B8G8R8A8_UNORM),
    std::pair(Format::BGRA8_SRGB, VK_FORMAT_B8G8R8A8_SRGB),
    std::pair(Format::RGBA16F, VK_FORMAT_R16G16B16A16_SFLOAT),
};
}

constexpr auto getVkFormat = enumMap<detail::format_map>;
constexpr auto getFormat = inverseEnumMap<detail::format_map>;

inline VkImageAspectFlags getFormatAspectFlags(Format format) {
  if (isColorFormat(format)) {
    return VK_IMAGE_ASPECT_COLOR_BIT;
  }
  bool is_depth = isDepthFormat(format);
  bool is_stencil = isStencilFormat(format);
  VkImageAspectFlags aspects = 0;
  if (is_depth) {
    aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  if (is_stencil) {
    aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  return aspects;
}
} // namespace ren
