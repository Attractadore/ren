#pragma once
#include "Formats.hpp"
#include "Support/Handle.hpp"

namespace ren {

struct TextureDesc {
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format;
  VkImageUsageFlags usage;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned depth = 1;
    unsigned array_layers;
  };
  unsigned mip_levels = 1;
};

struct TextureRef {
  TextureDesc desc;
  VkImage handle;
};

struct Texture {
  TextureDesc desc;
  SharedHandle<VkImage> handle;

  operator TextureRef() const {
    return {
        .desc = desc,
        .handle = handle.get(),
    };
  }
};

constexpr auto PARENT_FORMAT = VK_FORMAT_UNDEFINED;
constexpr unsigned ALL_MIP_LEVELS = -1;
constexpr unsigned ALL_ARRAY_LAYERS = -1;

struct TextureComponentMapping {
  VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle g = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle b = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_IDENTITY;

  constexpr auto operator<=>(const TextureComponentMapping &) const = default;
};

struct TextureViewDesc {
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = PARENT_FORMAT;
  TextureComponentMapping components;
  VkImageAspectFlags aspects;
  unsigned first_mip_level = 0;
  unsigned mip_levels = 1;
  unsigned first_array_layer = 0;
  unsigned array_layers = 1;

  constexpr auto operator<=>(const TextureViewDesc &) const = default;
};

struct TextureView {
  TextureViewDesc desc;
  VkImage texture;

  static TextureView create(const TextureRef &texture,
                            TextureViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {
      desc.format = texture.desc.format;
    }
    if (desc.mip_levels == ALL_MIP_LEVELS) {
      desc.mip_levels = texture.desc.mip_levels - desc.first_mip_level;
    }
    if (desc.array_layers == ALL_ARRAY_LAYERS) {
      desc.array_layers = texture.desc.array_layers - desc.first_array_layer;
    }
    return {.desc = desc, .texture = texture.handle};
  }
};

} // namespace ren
