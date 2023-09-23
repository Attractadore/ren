#pragma once
#include "Buffer.hpp"
#include "Handle.hpp"
#include "Passes/Exposure.hpp"
#include "Support/Span.hpp"

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
  BufferView vertex_positions;
  BufferView vertex_normals;
  BufferView vertex_colors;
  BufferView vertex_uvs;
  BufferView vertex_indices;
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
