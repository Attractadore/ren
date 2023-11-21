#pragma once
#include "Mesh.hpp"
#include "Passes/Exposure.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Mesh;
struct MeshInstance;
struct Pipelines;
class RgBuilder;

namespace glsl {
struct Material;
struct DirLight;
} // namespace glsl

struct OpaquePassesConfig {
  const Pipelines *pipelines = nullptr;
  ExposurePassOutput exposure;
  glm::uvec2 viewport;
  bool early_z = false;
};

struct OpaquePassesData {
  Span<const u32> batch_offsets;
  Span<const u32> batch_max_counts;
  Span<const VertexPoolList> vertex_pool_lists;
  Span<const Mesh> meshes;
  Span<const glsl::Material> materials;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::DirLight> directional_lights;

  glm::uvec2 viewport;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;

  bool instance_frustum_culling : 1 = true;
  bool early_z : 1 = false;
};

void setup_opaque_passes(RgBuilder &rgb, const OpaquePassesConfig &cfg);

auto set_opaque_passes_data(RenderGraph &rg, const OpaquePassesData &data)
    -> bool;

} // namespace ren
