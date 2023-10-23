#pragma once
#include "Buffer.hpp"
#include "Handle.hpp"
#include "Passes/Exposure.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

struct GraphicsPipeline;
struct Mesh;
struct MeshInstance;
class RgBuilder;

struct OpaquePassConfig {
  Handle<GraphicsPipeline> pipeline;
  ExposurePassOutput exposure;
  glm::uvec2 viewport_size;
};

struct OpaquePassData {
  Handle<Buffer> vertex_positions;
  Handle<Buffer> vertex_normals;
  Handle<Buffer> vertex_tangents;
  Handle<Buffer> vertex_colors;
  Handle<Buffer> vertex_uvs;
  Handle<Buffer> vertex_indices;
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
