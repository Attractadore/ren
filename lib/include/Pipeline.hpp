#pragma once
#include "Def.hpp"
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "ShaderStages.hpp"
#include "Support/Handle.hpp"
#include "Support/LinearMap.hpp"

#include <vulkan/vulkan.h>

#include <span>

namespace ren {

struct PushConstantRange {
  ShaderStageFlags stages;
  unsigned offset = 0;
  unsigned size;
};

struct PipelineSignatureDesc {
  SmallVector<DescriptorSetLayout, 4> set_layouts;
  SmallVector<PushConstantRange> push_constants;
};

struct PipelineSignatureRef {
  PipelineSignatureDesc *desc;
  VkPipelineLayout handle;
};

struct PipelineSignature {
  std::shared_ptr<PipelineSignatureDesc> desc;
  SharedHandle<VkPipelineLayout> handle;

  operator PipelineSignatureRef() const {
    return {.desc = desc.get(), .handle = handle.get()};
  }
};

#define REN_VERTEX_INPUT_RATES (Vertex)(Instance)
REN_DEFINE_ENUM(VertexInputRate, REN_VERTEX_INPUT_RATES);

struct VertexBinding {
  unsigned binding;
  unsigned stride;
  VertexInputRate rate = VertexInputRate::Vertex;
};

struct VertexAttribute {
  std::string semantic;
  unsigned location;
  unsigned count = 1;
  unsigned binding = 0;
  Format format;
  unsigned offset = 0;
};

#define REN_PRIMITIVE_TOPOLOGY_TYPES (Points)(Lines)(Triangles)
REN_DEFINE_ENUM(PrimitiveTopologyType, REN_PRIMITIVE_TOPOLOGY_TYPES);

#define REN_PRIMITIVE_TOPOLOGIES                                               \
  (PointList)(LineList)(                                                       \
      LineStrip)(TriangleList)(TriangleStrip)(LineListWithAdjacency)(LineStripWithAdjacency)(TriangleListWithAdjacency)(TriangleStripWithAdjacency)
REN_DEFINE_ENUM(PrimitiveTopology, REN_PRIMITIVE_TOPOLOGIES);

struct GraphicsPipelineDesc {
  struct IADesc {
    Vector<VertexBinding> bindings;
    Vector<VertexAttribute> attributes;
    std::variant<PrimitiveTopologyType, PrimitiveTopology> topology =
        PrimitiveTopology::TriangleList;
  } ia;

  struct MSDesc {
    unsigned samples = 1;
    unsigned sample_mask = -1;
  } ms;

  struct RTDesc {
    Format format;
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
  ShaderStage stage;
  std::span<const std::byte> code;
  std::string entry_point;
};

struct GraphicsPipelineConfig {
  PipelineSignatureRef signature;
  StaticVector<ShaderStageConfig, 2> shaders;
  GraphicsPipelineDesc desc;
};

class GraphicsPipelineBuilder {
  Device *m_device;
  GraphicsPipelineConfig m_config = {};

public:
  explicit GraphicsPipelineBuilder(Device &device) : m_device(&device) {}

  auto set_signature(PipelineSignatureRef signature)
      -> GraphicsPipelineBuilder & {
    m_config.signature = signature;
    return *this;
  }

  auto set_shader(ShaderStage stage, std::span<const std::byte> code,
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
    return set_shader(ShaderStage::Vertex, code, entry_point);
  }

  auto set_fragment_shader(std::span<const std::byte> code,
                           std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return set_shader(ShaderStage::Fragment, code, entry_point);
  }

  auto set_ia_vertex_bindings(Vector<VertexBinding> bindings)
      -> GraphicsPipelineBuilder & {
    m_config.desc.ia.bindings = std::move(bindings);
    return *this;
  }

  auto set_ia_vertex_attributes(Vector<VertexAttribute> attributes)
      -> GraphicsPipelineBuilder & {
    m_config.desc.ia.attributes = std::move(attributes);
    return *this;
  }

  auto set_render_target(Format format) -> GraphicsPipelineBuilder & {
    m_config.desc.rt = {.format = format};
    return *this;
  }

  auto build() -> GraphicsPipeline;
};

} // namespace ren
