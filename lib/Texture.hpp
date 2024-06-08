#pragma once
#include "DebugNames.hpp"
#include "Handle.hpp"
#include "Support/StdDef.hpp"
#include "ren/ren.hpp"

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>

namespace ren {

struct TextureCreateInfo {
  REN_DEBUG_NAME_FIELD("Texture");
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  u32 width = 0;
  u32 height = 1;
  u32 depth = 1;
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
};

struct Texture {
  VkImage image;
  VmaAllocation allocation;
  VkImageType type;
  VkFormat format;
  VkImageUsageFlags usage;
  union {
    struct {
      u32 width;
      u32 height;
      u32 depth;
    };
    glm::uvec3 size;
  };
  u32 num_mip_levels;
  u32 num_array_layers;
};

struct TextureSwizzle {
  VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle g = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle b = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_IDENTITY;

public:
  bool operator==(const TextureSwizzle &) const = default;
};

struct TextureView {
  Handle<Texture> texture;
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  TextureSwizzle swizzle;
  u32 first_mip_level = 0;
  u32 num_mip_levels = 0;
  u32 first_array_layer = 0;
  u32 num_array_layers = 0;

public:
  bool operator==(const TextureView &) const = default;
};

struct SamplerCreateInfo {
  REN_DEBUG_NAME_FIELD("Sampler");
  VkFilter mag_filter = VK_FILTER_LINEAR;
  VkFilter min_filter = VK_FILTER_LINEAR;
  VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  float anisotropy = 0.0f;

public:
  bool operator==(const SamplerCreateInfo &) const = default;
};

struct Sampler {
  VkSampler handle;
  VkFilter mag_filter;
  VkFilter min_filter;
  VkSamplerMipmapMode mipmap_mode;
  VkSamplerAddressMode address_mode_u;
  VkSamplerAddressMode address_mode_v;
  float anisotropy;
};

auto get_mip_level_count(unsigned width, unsigned height = 1,
                         unsigned depth = 1) -> u16;

auto get_size_at_mip_level(const glm::uvec3 &size, u16 mip_level) -> glm::uvec3;

auto getVkFilter(Filter filter) -> VkFilter;
auto getVkSamplerMipmapMode(Filter filter) -> VkSamplerMipmapMode;
auto getVkSamplerAddressMode(WrappingMode wrap) -> VkSamplerAddressMode;

REN_DEFINE_TYPE_HASH(SamplerCreateInfo, mag_filter, min_filter, mipmap_mode, address_mode_u, address_mode_v, anisotropy);

} // namespace ren
