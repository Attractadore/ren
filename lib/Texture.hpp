#pragma once
#include "DebugNames.hpp"
#include "core/GenIndex.hpp"
#include "core/StdDef.hpp"
#include "ren/ren.hpp"
#include "rhi.hpp"

#include <glm/glm.hpp>

namespace ren {

class Renderer;

struct TextureCreateInfo {
  REN_DEBUG_NAME_FIELD("Texture");
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  u32 width = 0;
  u32 height = 0;
  u32 depth = 0;
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
};

struct ExternalTextureCreateInfo {
  REN_DEBUG_NAME_FIELD("External Texture");
  rhi::Image handle = {};
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageUsageFlags usage;
  u32 width = 0;
  u32 height = 0;
  u32 depth = 0;
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
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
  u32 num_mip_levels = 0;
  u32 num_array_layers = 0;
};

constexpr u32 ALL_MIP_LEVELS = -1;

struct Subresource {
  Handle<Texture> handle;
  u32 first_mip_level = 0;
  u32 num_mip_levels = ALL_MIP_LEVELS;
};

struct SrvDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ComponentMapping components;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 first_mip_level = 0;
  u32 num_mip_levels = ALL_MIP_LEVELS;
};

struct UavDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 mip_level = 0;
};

struct RtvDesc {
  Handle<Texture> texture;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  rhi::ImageViewDimension dimension = rhi::ImageViewDimension::e2D;
  u32 mip_level = 0;
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

REN_DEFINE_TYPE_HASH(SamplerCreateInfo, mag_filter, min_filter, mipmap_mode,
                     address_mode_u, address_mode_v, anisotropy,
                     reduction_mode);

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

auto get_mip_level_count(u32 width, u32 height = 1, u32 depth = 1) -> u32;

auto get_mip_size(glm::uvec3 base_size, u32 mip_level) -> glm::uvec3;

auto get_texture_size(Renderer &renderer, Handle<Texture> texture,
                      u32 mip_level = 0) -> glm::uvec3;

auto get_rhi_Filter(Filter filter) -> rhi::Filter;
auto get_rhi_SamplerMipmapMode(Filter filter) -> rhi::SamplerMipmapMode;
auto get_rhi_SamplerAddressMode(WrappingMode wrap) -> rhi::SamplerAddressMode;

struct TextureBarrier {
  Subresource resource;
  rhi::PipelineStageMask src_stage_mask;
  rhi::AccessMask src_access_mask;
  rhi::ImageLayout src_layout = rhi::ImageLayout::Undefined;
  rhi::QueueFamily src_queue_family = {};
  rhi::PipelineStageMask dst_stage_mask;
  rhi::AccessMask dst_access_mask;
  rhi::ImageLayout dst_layout = rhi::ImageLayout::Undefined;
  rhi::QueueFamily dst_queue_family = {};
};

} // namespace ren
