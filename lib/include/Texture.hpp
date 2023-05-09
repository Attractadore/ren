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

struct TextureHandleView {
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
      -> TextureHandleView;

  operator Handle<Texture>() const;

  bool operator==(const TextureHandleView &other) const;
};

auto get_mip_level_count(unsigned width, unsigned height = 1,
                         unsigned depth = 1) -> unsigned;

struct SamplerDesc {
  VkFilter mag_filter;
  VkFilter min_filter;
  VkSamplerMipmapMode mipmap_mode;
  VkSamplerAddressMode address_Mode_u;
  VkSamplerAddressMode address_Mode_v;

public:
  constexpr auto operator<=>(const SamplerDesc &) const = default;
};

namespace detail {

template <typename S> class SamplerMixin {
  const S &impl() const { return *static_cast<const S *>(this); }
  S &impl() { return *static_cast<S *>(this); }

public:
  auto get_descriptor() const -> VkDescriptorImageInfo {
    return {.sampler = impl().get()};
  }

  bool operator==(const SamplerMixin &other) const {
    const auto &lhs = impl();
    const auto &rhs = other.impl();
    return lhs.get() == rhs.get();
  };
};

} // namespace detail

struct SamplerRef : detail::SamplerMixin<SamplerRef> {
  SamplerDesc desc;
  VkSampler handle;

public:
  auto get() const -> VkSampler { return handle; }
};

struct Sampler : detail::SamplerMixin<Sampler> {
  SamplerDesc desc;
  SharedHandle<VkSampler> handle;

public:
  auto get() const -> VkSampler { return handle.get(); }

  operator SamplerRef() const {
    return {
        .desc = desc,
        .handle = handle.get(),
    };
  }
};

auto getVkComponentSwizzle(RenTextureChannel channel) -> VkComponentSwizzle;
auto getVkComponentMapping(const RenTextureChannelSwizzle &swizzle)
    -> VkComponentMapping;

auto getVkFilter(RenFilter filter) -> VkFilter;
auto getVkSamplerMipmapMode(RenFilter filter) -> VkSamplerMipmapMode;
auto getVkSamplerAddressMode(RenWrappingMode wrap) -> VkSamplerAddressMode;

} // namespace ren
