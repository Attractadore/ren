#pragma once
#include "Formats.hpp"
#include "Support/Errors.hpp"

#include <vulkan/vulkan.h>

namespace ren {

inline FormatProperties getFormatProperties(VkFormat format) {
  using enum FormatProperty;
  switch (format) {
  default:
    unreachable("Unknown VkFormat {}", int(format));
  case VK_FORMAT_R8G8B8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_UNORM: {
    return {
        .flags = Color,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
        .alpha_bits = 8,
    };
  }
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 16,
        .green_bits = 16,
        .blue_bits = 16,
        .alpha_bits = 16,
    };
  case VK_FORMAT_R32G32B32_SFLOAT:
    return {
        .flags = Color,
        .red_bits = 32,
        .green_bits = 32,
        .blue_bits = 32,
    };
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_SRGB:
    return {
        .flags = Color | SRGB,
        .red_bits = 8,
        .green_bits = 8,
        .blue_bits = 8,
        .alpha_bits = 8,
    };
  }
}

inline bool isSRGBFormat(VkFormat format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::SRGB);
}

inline bool isColorFormat(VkFormat format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::Color);
}

inline bool isDepthFormat(VkFormat format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::Depth);
}

inline bool isStencilFormat(VkFormat format) {
  return getFormatProperties(format).flags.isSet(FormatProperty::Stencil);
}

inline VkFormat getSRGBFormat(VkFormat format) {
  using enum VkFormat;
  switch (format) {
  default:
    unreachable("VkFormat {} doesn't have an SRGB counterpart", int(format));
  case VK_FORMAT_R8G8B8A8_UNORM:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case VK_FORMAT_B8G8R8A8_UNORM:
    return VK_FORMAT_B8G8R8A8_SRGB;
  }
}

inline unsigned get_format_size(VkFormat format) {
  auto p = getFormatProperties(format);
  auto bits = [&] {
    if (isColorFormat(format)) {
      return p.red_bits + p.green_bits + p.blue_bits + p.alpha_bits;
    } else {
      return p.depth_bits + p.stencil_bits + p.unused_bits;
    }
  }();
  return bits / CHAR_BIT;
}

inline VkImageAspectFlags getVkImageAspectFlags(VkFormat format) {
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
