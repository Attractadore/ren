#pragma once
#include "DenseHandleMap.hpp"
#include "HandleMap.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "Pipeline.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct OpaquePassConfig {
  Handle<GraphicsPipeline> pipeline;
  RgBuffer transform_matrices;
  RgBuffer normal_matrices;
  RgBuffer directional_lights;
  RgBuffer materials;
  RgBuffer exposure;
  unsigned exposure_temporal_offset = 0;
};

struct OpaquePassOutput {
  RgPass pass;
  RgTexture texture;
};

struct OpaquePassData {
  const HandleMap<Mesh> *meshes = nullptr;
  Span<const MeshInst> mesh_insts;
  glm::uvec2 size;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
  u32 num_dir_lights = 0;
};

auto setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg)
    -> OpaquePassOutput;

} // namespace ren
