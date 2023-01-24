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

struct RenderTargetViewDesc {
  VkFormat format = PARENT_FORMAT;
  unsigned mip_level = 0;
  unsigned array_layer = 0;

  constexpr auto operator<=>(const RenderTargetViewDesc &) const = default;
};

struct RenderTargetView {
  RenderTargetViewDesc desc;
  TextureRef texture;

  static RenderTargetView create(const TextureRef &texture,
                                 RenderTargetViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {

      desc.format = texture.desc.format;
    }
    return {.desc = desc, .texture = texture};
  }
};

struct DepthStencilViewDesc {
  unsigned mip_level = 0;
  unsigned array_layer = 0;

  constexpr auto operator<=>(const DepthStencilViewDesc &) const = default;
};

struct DepthStencilView {
  DepthStencilViewDesc desc;
  TextureRef texture;

  static DepthStencilView create(const TextureRef &texture,
                                 DepthStencilViewDesc desc = {}) {
    return {.desc = desc, .texture = texture};
  }
};

struct TextureComponentMapping {
  VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle g = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle b = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_IDENTITY;

  constexpr auto operator<=>(const TextureComponentMapping &) const = default;
};

struct SampledTextureViewDesc {
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = PARENT_FORMAT;
  TextureComponentMapping components;
  unsigned first_mip_level = 0;
  unsigned mip_levels = ALL_MIP_LEVELS;
  unsigned first_array_layer = 0;
  unsigned array_layers = 1;

  constexpr auto operator<=>(const SampledTextureViewDesc &) const = default;
};

struct SampledTextureView {
  SampledTextureViewDesc desc;
  TextureRef texture;

  SampledTextureView create(const TextureRef &texture,
                            SampledTextureViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {
      desc.format = texture.desc.format;
    }
    if (desc.mip_levels == ALL_MIP_LEVELS) {
      desc.mip_levels = texture.desc.mip_levels - desc.first_mip_level;
    }
    if (desc.array_layers == ALL_ARRAY_LAYERS) {
      desc.array_layers = texture.desc.array_layers - desc.first_array_layer;
    }
    return {.desc = desc, .texture = texture};
  }
};

struct StorageTextureViewDesc {
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = PARENT_FORMAT;
  unsigned mip_level = 0;
  unsigned first_array_layer = 0;
  unsigned array_layers = 1;

  constexpr auto operator<=>(const StorageTextureViewDesc &) const = default;
};

struct StorageTextureView {
  StorageTextureViewDesc desc;
  TextureRef texture;

  static StorageTextureView create(const TextureRef &texture,
                                   StorageTextureViewDesc desc = {}) {
    if (desc.format == PARENT_FORMAT) {
      desc.format = texture.desc.format;
    }
    if (desc.array_layers == ALL_ARRAY_LAYERS) {
      desc.array_layers = texture.desc.array_layers - desc.first_array_layer;
    }
    return {.desc = desc, .texture = texture};
  }
};

} // namespace ren
