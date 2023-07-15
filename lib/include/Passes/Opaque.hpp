#pragma once
#include "DenseHandleMap.hpp"
#include "HandleMap.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "Pipeline.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct TextureIDAllocator;

struct OpaquePassConfig {
  const HandleMap<Mesh> *meshes = nullptr;
  std::span<const MeshInst> mesh_insts;
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  RGBufferID directional_lights_buffer;
  RGBufferID materials_buffer;
  RGBufferID exposure_buffer;
  Handle<GraphicsPipeline> pipeline;
  TextureIDAllocator *texture_allocator = nullptr;
  glm::uvec2 size;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
  unsigned num_dir_lights;
};

struct OpaquePassOutput {
  RGTextureID texture;
};

auto setup_opaque_pass(Device &device, RenderGraph::Builder &rgb,
                       const OpaquePassConfig &cfg) -> OpaquePassOutput;

} // namespace ren
