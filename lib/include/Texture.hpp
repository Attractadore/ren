#pragma once
#include "DebugNames.hpp"
#include "Handle.hpp"
#include "Support/Optional.hpp"
#include "Support/StdDef.hpp"
#include "ren/ren.h"

namespace ren {

class Device;

struct TextureCreateInfo {
  REN_DEBUG_NAME_FIELD("Texture");
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  u32 width = 1;
  u32 height = 1;
  union {
    u16 depth = 1;
    u16 array_layers;
  };
  u16 mip_levels = 1;
};

struct Texture {
  VkImage image;
  VmaAllocation allocation;
  VkImageType type;
  VkFormat format;
  VkImageUsageFlags usage;
  glm::uvec3 size;
  u16 num_mip_levels;
  u16 num_array_layers;
};

struct TextureSwizzle {
  VkComponentSwizzle r : 8 = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle g : 8 = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle b : 8 = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle a : 8 = VK_COMPONENT_SWIZZLE_IDENTITY;

public:
  bool operator==(const TextureSwizzle &) const = default;
};

struct TextureView {
  Handle<Texture> texture;
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  TextureSwizzle swizzle;
  u16 first_mip_level = 0;
  u16 num_mip_levels = 0;
  u16 first_array_layer = 0;
  u16 num_array_layers = 0;

public:
  static auto try_from_texture(const Device &device, Handle<Texture> texture)
      -> Optional<TextureView>;

  static auto from_texture(const Device &device, Handle<Texture> texture)
      -> TextureView;

  auto get_size(const Device &device, u16 mip_level_offset = 0) -> glm::uvec3;

  bool operator==(const TextureView &) const = default;
};

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
};

auto get_mip_level_count(unsigned width, unsigned height = 1,
                         unsigned depth = 1) -> u16;

auto get_size_at_mip_level(const glm::uvec3 &size, u16 mip_level) -> glm::uvec3;

auto getVkComponentSwizzle(RenTextureChannel channel) -> VkComponentSwizzle;
auto getTextureSwizzle(const RenTextureChannelSwizzle &swizzle)
    -> TextureSwizzle;

auto getVkFilter(RenFilter filter) -> VkFilter;
auto getVkSamplerMipmapMode(RenFilter filter) -> VkSamplerMipmapMode;
auto getVkSamplerAddressMode(RenWrappingMode wrap) -> VkSamplerAddressMode;

} // namespace ren
