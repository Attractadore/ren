#include "Texture.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "Support/Math.hpp"

namespace ren {

Texture::operator TextureView() const {
  return {
      .texture = *this,
      .type = get_texture_default_view_type(type, num_array_layers),
      .format = format,
      .num_mip_levels = num_mip_levels,
      .num_array_layers = num_array_layers,
  };
}

auto get_texture_default_view_type(VkImageType type, u32 num_array_layers)
    -> VkImageViewType {
  if (num_array_layers > 1) {
    switch (type) {
    default:
      break;
    case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
  } else {
    switch (type) {
    default:
      break;
    case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D;
    case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D;
    case VK_IMAGE_TYPE_3D:
      return VK_IMAGE_VIEW_TYPE_3D;
    }
  }
  unreachable("Invalid VkImageType/array_layers combination:", int(type),
              num_array_layers);
}

auto TextureView::operator->() const -> const Texture * {
  return &texture.get();
};

auto get_mip_level_count(unsigned width, unsigned height, unsigned depth)
    -> u16 {
  auto size = std::max({width, height, depth});
  return ilog2(size) + 1;
}

auto getVkComponentSwizzle(RenTextureChannel channel) -> VkComponentSwizzle {
  switch (channel) {
  case REN_TEXTURE_CHANNEL_IDENTITY:
    return VK_COMPONENT_SWIZZLE_IDENTITY;
  case REN_TEXTURE_CHANNEL_ZERO:
    return VK_COMPONENT_SWIZZLE_ZERO;
  case REN_TEXTURE_CHANNEL_ONE:
    return VK_COMPONENT_SWIZZLE_ONE;
  case REN_TEXTURE_CHANNEL_R:
    return VK_COMPONENT_SWIZZLE_R;
  case REN_TEXTURE_CHANNEL_G:
    return VK_COMPONENT_SWIZZLE_G;
  case REN_TEXTURE_CHANNEL_B:
    return VK_COMPONENT_SWIZZLE_B;
  case REN_TEXTURE_CHANNEL_A:
    return VK_COMPONENT_SWIZZLE_A;
  }
  unreachable("Unknown texture channel {}", int(channel));
}

auto getTextureSwizzle(const RenTextureChannelSwizzle &swizzle)
    -> TextureSwizzle {
  return {
      .r = getVkComponentSwizzle(swizzle.r),
      .g = getVkComponentSwizzle(swizzle.g),
      .b = getVkComponentSwizzle(swizzle.b),
      .a = getVkComponentSwizzle(swizzle.a),
  };
}

auto getVkFilter(RenFilter filter) -> VkFilter {
  switch (filter) {
  case REN_FILTER_NEAREST:
    return VK_FILTER_NEAREST;
  case REN_FILTER_LINEAR:
    return VK_FILTER_LINEAR;
  }
  unreachable("Unknown filter {}", int(filter));
}

auto getVkSamplerMipmapMode(RenFilter filter) -> VkSamplerMipmapMode {
  switch (filter) {
  case REN_FILTER_NEAREST:
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
  case REN_FILTER_LINEAR:
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
  unreachable("Unknown filter {}", int(filter));
}

auto getVkSamplerAddressMode(RenWrappingMode wrap) -> VkSamplerAddressMode {
  switch (wrap) {
  case REN_WRAPPING_MODE_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case REN_WRAPPING_MODE_MIRRORED_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case REN_WRAPPING_MODE_CLAMP_TO_EDGE:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  unreachable("Unknown wrapping mode {}", int(wrap));
}

} // namespace ren
