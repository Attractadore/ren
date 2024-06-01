#pragma once
#include "Camera.hpp"
#include "Mesh.hpp"
#include "Passes/Exposure.hpp"
#include "Support/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

const char SCENE_RUNTIME_CONFIG[] = "scene-runtime-config";
const char INSTANCE_CULLING_AND_LOD_RUNTIME_CONFIG[] =
    "instance-culling-and-lod-runtime-config";

struct Pipelines;
class RgBuilder;

namespace glsl {

struct Material;
struct DirLight;

} // namespace glsl

struct OpaquePassesConfig {
  const Pipelines *pipelines = nullptr;
  u32 num_meshes = 0;
  u32 num_mesh_instances = 0;
  u32 num_materials = 0;
  u32 num_directional_lights = 0;
  glm::uvec2 viewport;
  ExposurePassOutput exposure;
  bool early_z : 1 = false;
};

struct SceneRuntimeConfig {
  Camera camera;
  Span<const IndexPool> index_pools;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::Material> materials;
  Span<const glsl::DirLight> directional_lights;
};

struct InstanceCullingAndLODRuntimeConfig {
  i32 lod_bias = 0;
  float lod_triangle_pixels = 4.0f;
  bool frustum_culling : 1 = true;
  bool lod_selection : 1 = true;
};

void setup_opaque_passes(RgBuilder &rgb, const OpaquePassesConfig &cfg);

} // namespace ren
