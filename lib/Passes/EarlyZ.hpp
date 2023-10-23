#pragma once
#include "Buffer.hpp"
#include "Handle.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

struct GraphicsPipeline;
struct Mesh;
struct MeshInstance;
class RgBuilder;

struct EarlyZPassConfig {
  Handle<GraphicsPipeline> pipeline;
  glm::uvec2 viewport_size;
};

struct EarlyZPassData {
  Handle<Buffer> vertex_positions;
  Handle<Buffer> vertex_indices;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  glm::uvec2 viewport_size;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
};

void setup_early_z_pass(RgBuilder &rgb, const EarlyZPassConfig &cfg);

} // namespace ren
