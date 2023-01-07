#pragma once
#include "Pipeline.hpp"

#include <vulkan/vulkan.h>

namespace ren {

REN_MAP_TYPE(PrimitiveTopologyType, VkPrimitiveTopology);
REN_MAP_FIELD(PrimitiveTopologyType::Points, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
REN_MAP_FIELD(PrimitiveTopologyType::Lines, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
REN_MAP_FIELD(PrimitiveTopologyType::Triangles,
              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
REN_MAP_ENUM(getVkPrimitiveTopology, PrimitiveTopologyType,
             REN_PRIMITIVE_TOPOLOGY_TYPES);

REN_MAP_TYPE(PrimitiveTopology, VkPrimitiveTopology);
REN_MAP_FIELD(PrimitiveTopology::PointList, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
REN_MAP_FIELD(PrimitiveTopology::LineList, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
REN_MAP_FIELD(PrimitiveTopology::LineStrip, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
REN_MAP_FIELD(PrimitiveTopology::LineListWithAdjacency,
              VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY);
REN_MAP_FIELD(PrimitiveTopology::LineStripWithAdjacency,
              VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY);
REN_MAP_FIELD(PrimitiveTopology::TriangleList,
              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
REN_MAP_FIELD(PrimitiveTopology::TriangleStrip,
              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
REN_MAP_FIELD(PrimitiveTopology::TriangleListWithAdjacency,
              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY);
REN_MAP_FIELD(PrimitiveTopology::TriangleStripWithAdjacency,
              VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY);
REN_MAP_ENUM(getVkPrimitiveTopology, PrimitiveTopology,
             REN_PRIMITIVE_TOPOLOGIES);

inline VkPipeline getVkPipeline(PipelineRef pipeline) {
  return reinterpret_cast<VkPipeline>(pipeline.get());
}

inline VkPipelineLayout getVkPipelineLayout(PipelineSignatureRef signature) {
  return reinterpret_cast<VkPipelineLayout>(signature.get());
}

} // namespace ren
