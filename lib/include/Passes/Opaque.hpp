#pragma once
#include "HandleMap.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "Passes/Exposure.hpp"
#include "Pipeline.hpp"

namespace ren {

class RgBuilder;

struct OpaquePassConfig {
  Handle<GraphicsPipeline> pipeline;
  ExposurePassOutput exposure;
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

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg);

} // namespace ren
