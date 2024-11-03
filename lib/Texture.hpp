#pragma once
#include "DebugNames.hpp"
#include "Support/GenIndex.hpp"
#include "Support/StdDef.hpp"
#include "ren/ren.hpp"

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>

namespace ren {

struct TextureCreateInfo {
  REN_DEBUG_NAME_FIELD("Texture");
  VkImageType type = VK_IMAGE_TYPE_2D;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  VkImageUsageFlags usage = 0;
  u32 width = 0;
  u32 height = 1;
  u32 depth = 1;
  u32 num_mip_levels = 1;
  u32 num_array_layers = 1;
};

struct Texture {
  VkImage image;
  VmaAllocation allocation;
  VkImageType type;
  TinyImageFormat format;
  VkImageUsageFlags usage;
  union {
    struct {
      u32 width;
      u32 height;
      u32 depth;
    };
    glm::uvec3 size;
  };
  u32 num_mip_levels;
  u32 num_array_layers;
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

constexpr TextureState FS_READ_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr TextureState CS_READ_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr TextureState CS_WRITE_TEXTURE = {
    .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .layout = VK_IMAGE_LAYOUT_GENERAL,
};

constexpr TextureState CS_READ_WRITE_TEXTURE = {
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

struct TextureSwizzle {
  VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle g = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle b = VK_COMPONENT_SWIZZLE_IDENTITY;
  VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_IDENTITY;

public:
  bool operator==(const TextureSwizzle &) const = default;
};

struct TextureView {
  Handle<Texture> texture;
  VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  TextureSwizzle swizzle;
  u32 first_mip_level = 0;
  u32 num_mip_levels = 0;
  u32 first_array_layer = 0;
  u32 num_array_layers = 0;

public:
  bool operator==(const TextureView &) const = default;
};

enum class SamplerReductionMode {
  WeightedAverage,
  Min,
  Max,
  Last = Max,
};

struct SamplerCreateInfo {
  REN_DEBUG_NAME_FIELD("Sampler");
  VkFilter mag_filter = VK_FILTER_LINEAR;
  VkFilter min_filter = VK_FILTER_LINEAR;
  VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  float anisotropy = 0.0f;
  SamplerReductionMode reduction_mode = SamplerReductionMode::WeightedAverage;

public:
  bool operator==(const SamplerCreateInfo &) const = default;
};

REN_DEFINE_TYPE_HASH(SamplerCreateInfo, mag_filter, min_filter, mipmap_mode,
                     address_mode_u, address_mode_v, anisotropy,
                     reduction_mode);

struct Sampler {
  VkSampler handle;
  VkFilter mag_filter;
  VkFilter min_filter;
  VkSamplerMipmapMode mipmap_mode;
  VkSamplerAddressMode address_mode_u;
  VkSamplerAddressMode address_mode_v;
  float anisotropy;
  SamplerReductionMode reduction_mode;
};

auto get_mip_level_count(unsigned width, unsigned height = 1,
                         unsigned depth = 1) -> u16;

auto get_size_at_mip_level(const glm::uvec3 &size, u16 mip_level) -> glm::uvec3;

auto getVkFilter(Filter filter) -> VkFilter;
auto getVkSamplerMipmapMode(Filter filter) -> VkSamplerMipmapMode;
auto getVkSamplerAddressMode(WrappingMode wrap) -> VkSamplerAddressMode;

} // namespace ren
