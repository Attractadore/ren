#pragma once
#include "Mesh.hpp"
#include "Passes/Exposure.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

struct GraphicsPipeline;
struct Mesh;
struct MeshInstance;
class RgBuilder;

struct OpaquePassConfig {
  std::array<Handle<GraphicsPipeline>, NUM_MESH_ATTRIBUTE_FLAGS> pipelines;
  ExposurePassOutput exposure;
  glm::uvec2 viewport_size;
};

struct OpaquePassData {
  Span<const VertexPoolList> vertex_pool_lists;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  glm::uvec2 viewport_size;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
  u32 num_dir_lights = 0;
};

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg);

} // namespace ren
