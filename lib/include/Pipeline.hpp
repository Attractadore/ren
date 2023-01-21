#pragma once
#include "Def.hpp"
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "Support/Handle.hpp"
#include "Support/LinearMap.hpp"

#include <vulkan/vulkan.h>

#include <span>

namespace ren {

struct PushConstantRange {
  VkShaderStageFlags stages;
  unsigned offset = 0;
  unsigned size;
};

struct PipelineLayoutDesc {
  SmallVector<DescriptorSetLayout, 4> set_layouts;
  SmallVector<PushConstantRange> push_constants;
};

struct PipelineLayoutRef {
  PipelineLayoutDesc *desc;
  VkPipelineLayout handle;
};

struct PipelineLayout {
  std::shared_ptr<PipelineLayoutDesc> desc;
  SharedHandle<VkPipelineLayout> handle;

  operator PipelineLayoutRef() const {
    return {.desc = desc.get(), .handle = handle.get()};
  }
};

struct GraphicsPipelineDesc {
  struct IADesc {
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  } ia;

  struct MSDesc {
    unsigned samples = 1;
    unsigned sample_mask = -1;
  } ms;

  struct RTDesc {
    VkFormat format;
  } rt;
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

struct ShaderStageConfig {
  VkShaderStageFlagBits stage;
  std::span<const std::byte> code;
  std::string entry_point;
};

struct GraphicsPipelineConfig {
  PipelineLayoutRef signature;
  StaticVector<ShaderStageConfig, 2> shaders;
  GraphicsPipelineDesc desc;
};

class GraphicsPipelineBuilder {
  Device *m_device;
  GraphicsPipelineConfig m_config = {};

public:
  explicit GraphicsPipelineBuilder(Device &device) : m_device(&device) {}

  auto set_signature(PipelineLayoutRef signature) -> GraphicsPipelineBuilder & {
    m_config.signature = signature;
    return *this;
  }

  auto set_shader(VkShaderStageFlagBits stage, std::span<const std::byte> code,
                  std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    m_config.shaders.push_back({
        .stage = stage,
        .code = code,
        .entry_point = std::string(entry_point),
    });
    return *this;
  }

  auto set_vertex_shader(std::span<const std::byte> code,
                         std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return set_shader(VK_SHADER_STAGE_VERTEX_BIT, code, entry_point);
  }

  auto set_fragment_shader(std::span<const std::byte> code,
                           std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return set_shader(VK_SHADER_STAGE_FRAGMENT_BIT, code, entry_point);
  }

  auto set_render_target(VkFormat format) -> GraphicsPipelineBuilder & {
    m_config.desc.rt = {.format = format};
    return *this;
  }

  auto build() -> GraphicsPipeline;
};

} // namespace ren
