#pragma once
#include "Buffer.hpp"
#include "Config.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

struct ImGuiContext;

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
#if REN_IMGUI
  ImGuiContext *imgui_context = nullptr;
#endif
  const Pipelines *pipelines = nullptr;
  glm::uvec2 viewport_size;
  const PostProcessingOptions *pp_opts = nullptr;
  bool early_z : 1 = true;
};

struct PassesData {
  Handle<Buffer> vertex_positions;
  Handle<Buffer> vertex_normals;
  Handle<Buffer> vertex_tangents;
  Handle<Buffer> vertex_colors;
  Handle<Buffer> vertex_uvs;
  Handle<Buffer> vertex_indices;
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
