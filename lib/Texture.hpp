#pragma once
#include "core/GenIndex.hpp"
#include "ren/core/Array.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"
#include "ren/ren.hpp"
#include "rhi.hpp"

#include <glm/glm.hpp>

namespace ren {

class Renderer;

constexpr u32 MAX_SRV_SIZE = 4096;
constexpr u32 MAX_SRV_MIPS = 13;
static_assert(MAX_SRV_SIZE == (1 << (MAX_SRV_MIPS - 1)));

struct ImageView;

struct TextureCreateInfo {
  String8 name = "Texture";
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  u32 width = 1;
  u32 height = 1;
  u32 depth : 31 = 1;
  bool cube_map : 1 = false;
  u32 num_mips = 1;
  u32 num_layers = 1;
};

struct ExternalTextureCreateInfo {
  String8 name = "External Texture";
  rhi::Image handle = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  u32 width = 1;
  u32 height = 1;
  u32 depth = 1;
  u32 num_mips = 1;
  u32 num_layers = 1;
};

struct Texture {
  rhi::Image handle = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  union {
    struct {
      u32 width;
      u32 height;
      u32 depth;
    };
    glm::uvec3 size = {0, 0, 0};
  };
  bool cube_map = false;
  u32 num_mips = 0;
  u32 num_layers = 0;
  DynamicArray<ImageView> views;
};

constexpr u32 ALL_MIPS = -1;
constexpr u32 ALL_LAYERS = -1;

struct Subresource {
  Handle<Texture> handle;
  u32 base_mip = 0;
  u32 num_mips = ALL_MIPS;
};

struct SrvDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ComponentMapping components;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 base_mip = 0;
  u32 num_mips = ALL_MIPS;
  u32 base_layer = 0;
  u32 num_layers = ALL_LAYERS;
};

struct UavDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 mip = 0;
  u32 base_layer = 0;
  u32 num_layers = ALL_LAYERS;
};

struct RtvDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 mip = 0;
  u32 layer = 0;
};

auto get_mip_size(glm::uvec3 base_size, u32 mip) -> glm::uvec3;

auto get_mip_byte_size(TinyImageFormat format, glm::uvec3 size,
                       u32 num_layers = 1) -> usize;

auto get_mip_chain_length(u32 width, u32 height = 1, u32 depth = 1) -> u32;

auto get_mip_chain_byte_size(TinyImageFormat format, glm::uvec3 byte_size,
                             u32 num_layers, u32 base_mip, u32 num_mips)
    -> usize;

auto get_rhi_Filter(Filter filter) -> rhi::Filter;
auto get_rhi_SamplerMipmapMode(Filter filter) -> rhi::SamplerMipmapMode;
auto get_rhi_SamplerAddressMode(WrappingMode wrap) -> rhi::SamplerAddressMode;

struct TextureBarrier {
  Subresource resource;
  rhi::PipelineStageMask src_stage_mask;
  rhi::AccessMask src_access_mask;
  rhi::ImageLayout src_layout = rhi::ImageLayout::Undefined;
  rhi::PipelineStageMask dst_stage_mask;
  rhi::AccessMask dst_access_mask;
  rhi::ImageLayout dst_layout = rhi::ImageLayout::Undefined;
};

} // namespace ren
