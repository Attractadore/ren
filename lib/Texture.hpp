#pragma once
#include "DebugNames.hpp"
#include "core/GenIndex.hpp"
#include "core/StdDef.hpp"
#include "ren/ren.hpp"
#include "rhi.hpp"

#include <glm/glm.hpp>

namespace ren {

class Renderer;

constexpr u32 MAX_SRV_SIZE = 4096;
constexpr u32 MAX_SRV_MIPS = 13;
static_assert(MAX_SRV_SIZE == (1 << (MAX_SRV_MIPS - 1)));

struct TextureCreateInfo {
  REN_DEBUG_NAME_FIELD("Texture");
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  u32 width = 0;
  u32 height = 0;
  u32 depth : 31 = 0;
  bool cube_map : 1 = false;
  u32 num_mips = 1;
  u32 num_layers = 1;
};

struct ExternalTextureCreateInfo {
  REN_DEBUG_NAME_FIELD("External Texture");
  rhi::Image handle = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  u32 width = 0;
  u32 height = 0;
  u32 depth = 0;
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
};

constexpr u32 ALL_MIPS = -1;

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
};

struct UavDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 mip = 0;
};

struct RtvDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 mip = 0;
};

struct SamplerCreateInfo {
  rhi::Filter mag_filter = rhi::Filter::Linear;
  rhi::Filter min_filter = rhi::Filter::Linear;
  rhi::SamplerMipmapMode mipmap_mode = rhi::SamplerMipmapMode::Linear;
  rhi::SamplerAddressMode address_mode_u = rhi::SamplerAddressMode::Repeat;
  rhi::SamplerAddressMode address_mode_v = rhi::SamplerAddressMode::Repeat;
  rhi::SamplerAddressMode address_mode_w = rhi::SamplerAddressMode::Repeat;
  rhi::SamplerReductionMode reduction_mode =
      rhi::SamplerReductionMode::WeightedAverage;
  float anisotropy = 0.0f;

public:
  bool operator==(const SamplerCreateInfo &) const = default;
};

template <> struct Hash<SamplerCreateInfo> {
  auto operator()(const SamplerCreateInfo &value) const noexcept -> u64 {
    u64 seed = 0;
    seed = hash_combine(seed, value.mag_filter);
    seed = hash_combine(seed, value.min_filter);
    seed = hash_combine(seed, value.mipmap_mode);
    seed = hash_combine(seed, value.address_mode_u);
    seed = hash_combine(seed, value.address_mode_v);
    seed = hash_combine(seed, value.address_mode_w);
    seed = hash_combine(seed, value.reduction_mode);
    seed = hash_combine(seed, value.anisotropy);
    return seed;
  }
};

struct Sampler {
  rhi::Sampler handle = {};
  rhi::Filter mag_filter = {};
  rhi::Filter min_filter = {};
  rhi::SamplerMipmapMode mipmap_mode = {};
  rhi::SamplerAddressMode address_mode_u = {};
  rhi::SamplerAddressMode address_mode_v = {};
  rhi::SamplerAddressMode address_mode_w = {};
  rhi::SamplerReductionMode reduction_mode = {};
  float anisotropy = 0.0f;
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
