#include "Texture.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Formats.inl"
#include "Support/Math.hpp"

namespace ren {

static auto get_texture_view_type(const Texture &texture) -> VkImageViewType {
  if (texture.array_layers > 1) {
    switch (texture.type) {
    default:
      break;
    case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
  } else {
    switch (texture.type) {
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
  unreachable("Invalid VkImageType/array_layers combination:",
              int(texture.type), texture.array_layers);
}

auto TextureHandleView::from_texture(const Device &device,
                                     Handle<Texture> handle)
    -> TextureHandleView {
  const auto &texture = device.get_texture(handle);

  auto view_type = get_texture_view_type(texture);

  return {
      .texture = handle,
      .type = view_type,
      .format = texture.format,
      .subresource =
          {
              .aspectMask = getVkImageAspectFlags(texture.format),
              .levelCount = texture.mip_levels,
              .layerCount = texture.array_layers,
          },
  };
}

TextureHandleView::operator Handle<Texture>() const { return texture; }

bool TextureHandleView::operator==(const TextureHandleView &other) const {
  static_assert(sizeof(other) == sizeof(texture) + sizeof(type) +
                                     sizeof(format) + sizeof(swizzle) +
                                     sizeof(subresource));
  return std::memcmp(this, &other, sizeof(other)) == 0;
}

auto get_mip_level_count(unsigned width, unsigned height, unsigned depth)
    -> unsigned {
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

auto getVkComponentMapping(const RenTextureChannelSwizzle &swizzle)
    -> VkComponentMapping {
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
