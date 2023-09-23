#pragma once
#include "Buffer.hpp"
#include "Support/Span.hpp"

namespace ren {

class RenderGraph;
class CommandAllocator;
struct Camera;
struct Mesh;
struct MeshInstance;
struct Pipelines;
struct PostProcessingOptions;

namespace glsl {

struct DirLight;
struct Material;

} // namespace glsl

struct PassesConfig {
  const Pipelines *pipelines = nullptr;
  glm::uvec2 viewport_size;
  const PostProcessingOptions *pp_opts = nullptr;
};

struct PassesData {
  BufferView vertex_positions;
  BufferView vertex_normals;
  BufferView vertex_colors;
  BufferView vertex_uvs;
  BufferView vertex_indices;
  Span<const Mesh> meshes;
  Span<const glsl::Material> materials;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::DirLight> directional_lights;
  glm::uvec2 viewport_size;
  const Camera *camera = nullptr;
  const PostProcessingOptions *pp_opts;
};

void update_rg_passes(RenderGraph &rg, CommandAllocator &cmd_alloc,
                      const PassesConfig &cfg, const PassesData &data);

} // namespace ren
