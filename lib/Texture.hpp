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

struct TextureState {
  /// Pipeline stages in which the texture is accessed
  VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
  /// Types of accesses performed on the texture
  VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
  /// Layout of the texture
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

constexpr TextureState VS_SAMPLE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
};

constexpr TextureState FS_SAMPLE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
};

constexpr TextureState CS_SAMPLE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
};

constexpr TextureState CS_UAV_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr TextureState COLOR_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
};

constexpr TextureState READ_WRITE_DEPTH_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
};

constexpr TextureState READ_DEPTH_ATTACHMENT = {
    .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
    .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
};

constexpr TextureState TRANSFER_SRC_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
};

constexpr TextureState TRANSFER_DST_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
};

constexpr TextureState PRESENT_SRC_TEXTURE = {
    .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
};

constexpr u32 ALL_MIP_LEVELS = -1;

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

} // namespace ren
