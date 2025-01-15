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

auto get_rhi_Filter(Filter filter) -> rhi::Filter {
  switch (filter) {
  default:
    unreachable("Unknown filter {}", int(filter));
  case Filter::Nearest:
    return rhi::Filter::Nearest;
  case Filter::Linear:
    return rhi::Filter::Linear;
  }
}

auto get_rhi_SamplerMipmapMode(Filter filter) -> rhi::SamplerMipmapMode {
  switch (filter) {
  default:
    unreachable("Unknown filter {}", int(filter));
  case Filter::Nearest:
    return rhi::SamplerMipmapMode::Nearest;
  case Filter::Linear:
    return rhi::SamplerMipmapMode::Linear;
  }
}

auto get_rhi_SamplerAddressMode(WrappingMode wrap) -> rhi::SamplerAddressMode {
  switch (wrap) {
  default:
    unreachable("Unknown wrapping mode {}", int(wrap));
  case WrappingMode::Repeat:
    return rhi::SamplerAddressMode::Repeat;
  case WrappingMode::MirroredRepeat:
    return rhi::SamplerAddressMode::MirroredRepeat;
  case WrappingMode::ClampToEdge:
    return rhi::SamplerAddressMode::ClampToEdge;
  }
}

} // namespace ren
