#pragma once
#include "RenderGraph.hpp"

#include <glm/glm.hpp>

namespace ren {

namespace glsl {
struct Mesh;
struct Material;
struct MeshInstance;
struct DirectionalLight;
} // namespace glsl

struct GpuScene {
  StatefulBufferSlice<glsl::Mesh> meshes;
  StatefulBufferSlice<glsl::MeshInstance> mesh_instances;
  StatefulBufferSlice<glsl::Material> materials;
  StatefulBufferSlice<glsl::DirectionalLight> directional_lights;
};

auto init_gpu_scene(ResourceArena &arena) -> GpuScene;

struct RgGpuScene {
  RgBufferId<glsl::Mesh> meshes;
  RgBufferId<glsl::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  RgBufferId<glm::mat3> normal_matrices;
  RgBufferId<glsl::Material> materials;
  RgBufferId<glsl::DirectionalLight> directional_lights;
};

} // namespace ren
