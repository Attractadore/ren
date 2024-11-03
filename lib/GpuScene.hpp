#pragma once
#include "Batch.hpp"
#include "DrawSet.hpp"
#include "Light.hpp"
#include "Material.hpp"
#include "RenderGraph.hpp"
#include "glsl/Culling.h"
#include "glsl/GpuScene.h"

#include <glm/glm.hpp>

namespace ren {

struct Mesh;
struct MeshInstance;
struct Pipelines;
struct SceneData;

namespace glsl {
struct Mesh;
struct Material;
struct MeshInstance;
struct DirectionalLight;
} // namespace glsl

struct Batch {
  BatchDesc desc;
  u32 num_meshlets = 0;
};

struct DrawSetData {
  Vector<Handle<MeshInstance>> mesh_instances;
  StatefulBufferSlice<glsl::InstanceCullData> cull_data;
  Vector<DrawSetId> update_ids;
  Vector<glsl::InstanceCullData> update_cull_data;
  Vector<DrawSetId> delete_ids;
  Vector<Batch> batches;
};

using MeshInstanceVisibilityMask = GLSL_MESH_INSTANCE_VISIBILITY_MASK;

struct GpuScene {
  StatefulBufferSlice<glsl::Mesh> meshes;
  Vector<Handle<Mesh>> update_meshes;
  Vector<glsl::Mesh> mesh_update_data;

  StatefulBufferSlice<glsl::MeshInstance> mesh_instances;
  StatefulBufferSlice<MeshInstanceVisibilityMask> mesh_instance_visibility;
  Vector<Handle<MeshInstance>> update_mesh_instances;
  Vector<glsl::MeshInstance> mesh_instance_update_data;
  std::array<DrawSetData, NUM_DRAW_SETS> draw_sets;

  StatefulBufferSlice<glsl::Material> materials;
  Vector<Handle<Material>> update_materials;
  Vector<glsl::Material> material_update_data;

  StatefulBufferSlice<glsl::DirectionalLight> directional_lights;
  Vector<Handle<DirectionalLight>> update_directional_lights;
  Vector<glsl::DirectionalLight> directional_light_update_data;
};

auto init_gpu_scene(ResourceArena &arena) -> GpuScene;

struct RgDrawSetData {
  RgBufferId<glsl::InstanceCullData> cull_data;
};

struct RgGpuScene {
  RgBufferId<glsl::Mesh> meshes;
  RgBufferId<glsl::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  RgBufferId<glm::mat3> normal_matrices;
  RgBufferId<MeshInstanceVisibilityMask> mesh_instance_visibility;
  std::array<RgDrawSetData, NUM_DRAW_SETS> draw_sets;
  RgBufferId<glsl::Material> materials;
  RgBufferId<glsl::DirectionalLight> directional_lights;
};

void add_to_draw_set(SceneData &scene, GpuScene &gpu_scene,
                     const Pipelines &pipelines, Handle<MeshInstance> handle,
                     DrawSet set);

void remove_from_draw_set(SceneData &scene, GpuScene &gpu_scene,
                          const Pipelines &pipelines,
                          Handle<MeshInstance> handle, DrawSet set);

} // namespace ren
