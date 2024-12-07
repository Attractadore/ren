#include "Texture.hpp"
#include "core/Errors.hpp"
#include "core/Math.hpp"

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

auto getVkFilter(Filter filter) -> VkFilter {
  switch (filter) {
  default:
    unreachable("Unknown filter {}", int(filter));
  case Filter::Nearest:
    return VK_FILTER_NEAREST;
  case Filter::Linear:
    return VK_FILTER_LINEAR;
  }
}

auto getVkSamplerMipmapMode(Filter filter) -> VkSamplerMipmapMode {
  switch (filter) {
  default:
    unreachable("Unknown filter {}", int(filter));
  case Filter::Nearest:
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
  case Filter::Linear:
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
}

auto getVkSamplerAddressMode(WrappingMode wrap) -> VkSamplerAddressMode {
  switch (wrap) {
  default:
    unreachable("Unknown wrapping mode {}", int(wrap));
  case WrappingMode::Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case WrappingMode::MirroredRepeat:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case WrappingMode::ClampToEdge:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

} // namespace ren
