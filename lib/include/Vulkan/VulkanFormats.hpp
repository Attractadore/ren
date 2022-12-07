#pragma once
#include "Formats.inl"

#include <vulkan/vulkan.h>

#include <cassert>

namespace ren {
REN_MAP_TYPE(Format, VkFormat);
#if 0
REN_MAP_FIELD(Format::RGB8, VK_FORMAT_R8G8B8_UNORM);
REN_MAP_FIELD(Format::RGB8_SRGB, VK_FORMAT_R8G8B8_SRGB);
REN_MAP_FIELD(Format::BGR8, VK_FORMAT_B8G8R8_UNORM);
REN_MAP_FIELD(Format::BGR8_SRGB, VK_FORMAT_B8G8R8_SRGB);
#endif
REN_MAP_FIELD(Format::RGBA8, VK_FORMAT_R8G8B8A8_UNORM);
REN_MAP_FIELD(Format::RGBA8_SRGB, VK_FORMAT_R8G8B8A8_SRGB);
REN_MAP_FIELD(Format::BGRA8, VK_FORMAT_B8G8R8A8_UNORM);
REN_MAP_FIELD(Format::BGRA8_SRGB, VK_FORMAT_B8G8R8A8_SRGB);
REN_MAP_FIELD(Format::RGBA16F, VK_FORMAT_R16G16B16A16_SFLOAT);

REN_MAP_ENUM(getVkFormat, Format, REN_FORMATS);
REN_REVERSE_MAP_ENUM(getFormat, Format, REN_FORMATS);

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
