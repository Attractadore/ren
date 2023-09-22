#pragma once
#include "Config.hpp"
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "Support/Span.hpp"

namespace ren {

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

struct ShaderInfo {
  Span<const std::byte> code;
  const char *entry_point = "main";
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
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool write_depth = true;
  VkCompareOp compare_op = VK_COMPARE_OP_GREATER;
};

struct ColorAttachmentInfo {
  VkFormat format;
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
