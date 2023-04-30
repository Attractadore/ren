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

inline constexpr auto operator<=>(const VkComponentMapping &lhs,
                                  const VkComponentMapping &rhs) {
  return std::array{lhs.r, lhs.g, lhs.b, lhs.a} <=>
         std::array{rhs.r, rhs.g, rhs.b, rhs.a};
}

inline constexpr auto operator==(const VkComponentMapping &lhs,
                                 const VkComponentMapping &rhs) {
  return std::array{lhs.r, lhs.g, lhs.b, lhs.a} ==
         std::array{rhs.r, rhs.g, rhs.b, rhs.a};
}

struct TextureViewDesc {
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  VkFormat format = PARENT_FORMAT;
  VkComponentMapping swizzle = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
  };
  VkImageAspectFlags aspects = 0;
  unsigned first_mip_level = 0;
  unsigned mip_levels = 1;
  unsigned first_array_layer = 0;
  unsigned array_layers = 1;

  constexpr auto operator<=>(const TextureViewDesc &) const = default;
};

struct TextureView {
  TextureViewDesc desc;
  VkImage texture;

public:
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
