#pragma once
#include "Camera.hpp"
#include "Mesh.hpp"
#include "PostProcessingOptions.hpp"
#include "Support/Span.hpp"
#include "ren/ren.hpp"

#include <glm/glm.hpp>

struct ImGuiContext;

namespace ren {

class RenderGraph;
class RgBuilder;
class CommandAllocator;
struct Camera;
struct Pipelines;
struct PostProcessingOptions;

namespace glsl {

struct DirLight;
struct Material;

} // namespace glsl

struct PassesConfig {
  const Pipelines *pipelines = nullptr;

  u32 num_meshes = 0;
  u32 num_mesh_instances = 0;
  u32 num_materials = 0;
  u32 num_directional_lights = 0;
  glm::uvec2 viewport;

  ExposureMode exposure;

  // ImGui
#if REN_IMGUI
  ImGuiContext *imgui_context = nullptr;
  u32 num_imgui_vertices = 0;
  u32 num_imgui_indices = 0;
#endif

  bool early_z : 1 = true;
};

void setup_render_graph(RgBuilder &rgb, const PassesConfig &cfg);

struct PassesRuntimeConfig {
  Camera camera;
  Span<const IndexPool> index_pools;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::Material> materials;
  Span<const glsl::DirLight> directional_lights;

  PostProcessingOptions pp_opts;

  // Instance culling
  float lod_triangle_pixels = 4.0f;
  i32 lod_bias = 0;

  bool instance_frustum_culling : 1 = true;
  bool lod_selection : 1 = true;
};

void update_render_graph(RenderGraph &rg, const PassesConfig &cfg,
                         const PassesRuntimeConfig &rt_cfg);

} // namespace ren
