#pragma once
#include "Config.hpp"
#include "DebugNames.hpp"
#include "Formats.hpp"
#include "core/GenIndex.hpp"
#include "core/Optional.hpp"
#include "core/Span.hpp"
#include "core/Variant.hpp"
#include "core/Vector.hpp"

namespace ren {

struct DescriptorSetLayout;

struct DynamicState {};
constexpr DynamicState DYNAMIC;

struct PipelineLayoutCreateInfo {
  REN_DEBUG_NAME_FIELD("Pipeline layout");
  TempSpan<const Handle<DescriptorSetLayout>> set_layouts;
  VkPushConstantRange push_constants;
};

struct PipelineLayout {
  VkPipelineLayout handle;
  StaticVector<Handle<DescriptorSetLayout>, MAX_DESCRIPTOR_SETS> set_layouts;
  VkPushConstantRange push_constants;
};

struct SpecConstant {
  u32 id = -1;
  u32 value = 0;
};

struct ShaderInfo {
  Span<const std::byte> code;
  const char *entry_point = "main";
  Span<const SpecConstant> spec_constants;
};

struct InputAssemblyInfo {
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

struct RasterizationInfo {
  VkCullModeFlagBits cull_mode = VK_CULL_MODE_BACK_BIT;
  VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
};

struct MultisampleInfo {
  u32 samples = 1;
};

struct DepthTestInfo {
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  bool write_depth = true;
  Variant<VkCompareOp, DynamicState> compare_op = DYNAMIC;
};

struct ColorBlendAttachmentInfo {
  VkBlendFactor src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA;
  VkBlendFactor dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  VkBlendOp color_blend_op = VK_BLEND_OP_ADD;
  VkBlendFactor src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
  VkBlendFactor dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  VkBlendOp alpha_blend_op = VK_BLEND_OP_ADD;
};

struct ColorAttachmentInfo {
  TinyImageFormat format = TinyImageFormat_UNDEFINED;
  Optional<ColorBlendAttachmentInfo> blending;
  VkColorComponentFlags write_mask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
};

struct GraphicsPipelineCreateInfo {
  REN_DEBUG_NAME_FIELD("Graphics pipeline");
  Handle<PipelineLayout> layout;
  ShaderInfo vertex_shader;
  Optional<ShaderInfo> fragment_shader;
  InputAssemblyInfo input_assembly;
  RasterizationInfo rasterization;
  MultisampleInfo multisample;
  Optional<DepthTestInfo> depth_test;
  TempSpan<const ColorAttachmentInfo> color_attachments;
};

struct GraphicsPipeline {
  VkPipeline handle;
  Handle<PipelineLayout> layout;
  VkShaderStageFlags stages = 0;
  InputAssemblyInfo input_assembly;
  MultisampleInfo multisample;
  Optional<DepthTestInfo> depth_test;
  StaticVector<ColorAttachmentInfo, MAX_COLOR_ATTACHMENTS> color_attachments;
};

struct ComputePipelineCreateInfo {
  REN_DEBUG_NAME_FIELD("Compute pipeline");
  Handle<PipelineLayout> layout;
  ShaderInfo shader;
};

struct ComputePipeline {
  VkPipeline handle;
  Handle<PipelineLayout> layout;
};

} // namespace ren
