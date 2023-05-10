#pragma once
#include "DebugNames.hpp"
#include "Handle.hpp"
#include "Support/Handle.hpp"
#include "ren/ren.h"

namespace ren {

class Device;

struct TextureCreateInfo {
  REN_DEBUG_NAME_FIELD("Texture");
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  unsigned width = 1;
  unsigned height = 1;
  union {
    unsigned depth = 1;
    unsigned array_layers;
  };
  unsigned mip_levels = 1;
};

struct Texture {
  VkImage image;
  VmaAllocation allocation;
  VkImageType type;
  VkFormat format;
  VkImageUsageFlags usage;
  unsigned width;
  unsigned height;
  unsigned depth;
  unsigned mip_levels;
  unsigned array_layers;
};

struct TextureView {
  Handle<Texture> texture;
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkComponentMapping swizzle = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
  };
  VkImageSubresourceRange subresource = {
      .aspectMask = 0,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };

public:
  static auto from_texture(const Device &device, Handle<Texture> texture)
      -> TextureView;

  operator Handle<Texture>() const;

  bool operator==(const TextureView &other) const;
};

auto get_mip_level_count(unsigned width, unsigned height = 1,
                         unsigned depth = 1) -> unsigned;

struct SamplerCreateInfo {
  REN_DEBUG_NAME_FIELD("Sampler");
  VkFilter mag_filter;
  VkFilter min_filter;
  VkSamplerMipmapMode mipmap_mode;
  VkSamplerAddressMode address_mode_u;
  VkSamplerAddressMode address_mode_v;
};

struct Sampler {
  VkSampler handle;
  VkFilter mag_filter;
  VkFilter min_filter;
  VkSamplerMipmapMode mipmap_mode;
  VkSamplerAddressMode address_mode_u;
  VkSamplerAddressMode address_mode_v;

public:
  auto get_descriptor() const -> VkDescriptorImageInfo;
};

auto getVkComponentSwizzle(RenTextureChannel channel) -> VkComponentSwizzle;
auto getVkComponentMapping(const RenTextureChannelSwizzle &swizzle)
    -> VkComponentMapping;

auto getVkFilter(RenFilter filter) -> VkFilter;
auto getVkSamplerMipmapMode(RenFilter filter) -> VkSamplerMipmapMode;
auto getVkSamplerAddressMode(RenWrappingMode wrap) -> VkSamplerAddressMode;

} // namespace ren
