#include "Texture.hpp"
#include "Formats.hpp"
#include "Renderer.hpp"
#include "Support/Errors.hpp"
#include "Support/Math.hpp"

namespace ren {

auto get_mip_level_count(unsigned width, unsigned height, unsigned depth)
    -> u16 {
  auto size = std::max({width, height, depth});
  return ilog2(size) + 1;
}

auto get_size_at_mip_level(const glm::uvec3 &size, u16 mip_level)
    -> glm::uvec3 {
  return glm::max(size >> glm::uvec3(mip_level), 1u);
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
