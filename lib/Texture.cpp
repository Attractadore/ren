#include "Texture.hpp"
#include "Renderer.hpp"
#include "core/Errors.hpp"
#include "core/Math.hpp"
#include "core/Views.hpp"

namespace ren {

auto get_mip_level_count(u32 width, u32 height, u32 depth) -> u32 {
  auto size = std::max({width, height, depth});
  return ilog2(size) + 1;
}

auto get_mip_size(glm::uvec3 size, u32 mip_level) -> glm::uvec3 {
  return glm::max(size >> glm::uvec3(mip_level), 1u);
}

auto get_texture_size(Renderer &renderer, Handle<Texture> texture,
                      u32 mip_level) -> glm::uvec3 {
  return get_mip_size(renderer.get_texture(texture).size, mip_level);
}

auto get_mip_byte_size(TinyImageFormat format, glm::uvec3 size, u32 num_layers)
    -> usize {
  ren_assert(glm::all(glm::greaterThan(size, glm::uvec3(0))));
  glm::uvec3 block_size = {
      TinyImageFormat_WidthOfBlock(format),
      TinyImageFormat_HeightOfBlock(format),
      TinyImageFormat_DepthOfBlock(format),
  };
  glm::uvec3 num_blocks = (size + block_size - glm::uvec3(1)) / block_size;
  return TinyImageFormat_BitSizeOfBlock(format) / 8 * num_blocks.x *
         num_blocks.y * num_blocks.z * num_layers;
}

auto get_mip_chain_byte_size(TinyImageFormat format, glm::uvec3 base_size,
                             u32 num_layers, u32 base_mip, u32 num_mips)
    -> usize {
  usize block_byte_size = TinyImageFormat_BitSizeOfBlock(format) / 8;
  glm::uvec3 block_size = {
      TinyImageFormat_WidthOfBlock(format),
      TinyImageFormat_HeightOfBlock(format),
      TinyImageFormat_DepthOfBlock(format),
  };
  usize byte_size = 0;
  for (u32 mip : range(base_mip, base_mip + num_mips)) {
    glm::uvec3 mip_size = glm::max(base_size >> mip, {1, 1, 1});
    glm::uvec3 num_blocks =
        (mip_size + block_size - glm::uvec3(1)) / block_size;
    byte_size += block_byte_size * num_blocks.x * num_blocks.y * num_blocks.z *
                 num_layers;
  }
  return byte_size;
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
