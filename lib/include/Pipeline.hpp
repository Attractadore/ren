#pragma once
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "Support/Handle.hpp"

#include <span>

namespace ren {

class Device;

struct PipelineLayoutCreateInfo {
  REN_DEBUG_NAME_FIELD("Pipeline layout");
  std::span<const Handle<DescriptorSetLayout>> set_layouts;
  VkPushConstantRange push_constants;
};

struct PipelineLayout {
  VkPipelineLayout handle;
  StaticVector<Handle<DescriptorSetLayout>, MAX_DESCRIPTOR_SETS> set_layouts;
  VkPushConstantRange push_constants;
};

struct GraphicsPipelineDesc {
  VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthWriteEnable = true,
      .depthCompareOp = VK_COMPARE_OP_GREATER,
  };

  Vector<VkPipelineColorBlendAttachmentState> render_target_blend_infos;
  VkPipelineColorBlendStateCreateInfo blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
  };

  Vector<VkFormat> render_target_formats;
  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
  };

  Vector<VkDynamicState> dynamic_states = {
      VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
      VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
  };
  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
  };
};

struct GraphicsPipelineRef {
  std::shared_ptr<GraphicsPipelineDesc> desc;
  VkPipeline handle;
};

struct GraphicsPipeline {
  std::shared_ptr<GraphicsPipelineDesc> desc;
  SharedHandle<VkPipeline> handle;

  operator GraphicsPipelineRef() const {
    return {.desc = desc, .handle = handle.get()};
  }
};

struct GraphicsPipelineConfig {
  Handle<PipelineLayout> layout;
  StaticVector<std::string, 2> entry_points;
  StaticVector<VkPipelineShaderStageCreateInfo, 2> shaders;
  GraphicsPipelineDesc desc;
};

class GraphicsPipelineBuilder {
  Device *m_device;
  GraphicsPipelineConfig m_config = {};

public:
  explicit GraphicsPipelineBuilder(Device &device) : m_device(&device) {}

  auto set_layout(Handle<PipelineLayout> layout) -> GraphicsPipelineBuilder & {
    m_config.layout = layout;
    return *this;
  }

  auto add_shader(VkShaderStageFlagBits stage, VkShaderModule code,
                  std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    m_config.entry_points.emplace_back(entry_point);
    m_config.shaders.push_back({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = code,
    });
    return *this;
  }

  auto add_vertex_shader(VkShaderModule code,
                         std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return add_shader(VK_SHADER_STAGE_VERTEX_BIT, code, entry_point);
  }

  auto add_fragment_shader(VkShaderModule code,
                           std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, code, entry_point);
  }

  auto add_render_target(VkFormat format) -> GraphicsPipelineBuilder & {
    m_config.desc.render_target_formats.push_back(format);
    m_config.desc.render_target_blend_infos.push_back({
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    });
    return *this;
  }

  auto add_depth_target(VkFormat format) -> GraphicsPipelineBuilder & {
    m_config.desc.rendering_info.depthAttachmentFormat = format;
    m_config.desc.depth_stencil_info.depthTestEnable = true;
    return *this;
  }

  auto build() -> GraphicsPipeline;
};

} // namespace ren
