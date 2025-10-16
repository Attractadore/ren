#pragma once
#include "DrawSet.hpp"
#include "Mesh.hpp"
#include "RenderGraph.hpp"
#include "sh/Geometry.h"
#include "sh/Lighting.h"

#include <glm/glm.hpp>

namespace ren {

struct SceneData;
struct Pipelines;

struct BatchDesc {
  MeshAttributeFlags attributes;
  u32 index_pool = -1;

  bool operator==(const BatchDesc &) const = default;
};

struct Batch {
  BatchDesc desc;
  u32 num_meshlets = 0;
};

struct DrawSetData {
public:
  Vector<Handle<MeshInstance>> mesh_instances;
  BufferSlice<sh::InstanceCullData> cull_data;
  Vector<sh::InstanceCullData> update_cull_data;
  Vector<DrawSetId> delete_ids;
  Vector<Batch> batches;

public:
  auto size() const -> usize {
    return mesh_instances.size() - delete_ids.size();
  }
};

struct GpuScene {
  BufferSlice<float> exposure;

  BufferSlice<sh::Mesh> meshes;
  Vector<Handle<Mesh>> update_meshes;
  Vector<sh::Mesh> mesh_update_data;

  BufferSlice<sh::MeshInstance> mesh_instances;
  BufferSlice<sh::MeshInstanceVisibilityMask> mesh_instance_visibility;
  Vector<Handle<MeshInstance>> update_mesh_instances;
  Vector<sh::MeshInstance> mesh_instance_update_data;
  std::array<DrawSetData, NUM_DRAW_SETS> draw_sets;

  BufferSlice<sh::Material> materials;
  Vector<Handle<Material>> update_materials;
  Vector<sh::Material> material_update_data;

  BufferSlice<sh::DirectionalLight> directional_lights;
  Vector<Handle<DirectionalLight>> update_directional_lights;
  Vector<sh::DirectionalLight> directional_light_update_data;
};

auto init_gpu_scene(ResourceArena &arena) -> GpuScene;

struct RgDrawSetData {
  RgBufferId<sh::InstanceCullData> cull_data;
};

struct RgGpuScene {
  RgBufferId<float> exposure;
  RgBufferId<sh::Mesh> meshes;
  RgBufferId<sh::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  RgBufferId<sh::MeshInstanceVisibilityMask> mesh_instance_visibility;
  std::array<RgDrawSetData, NUM_DRAW_SETS> draw_sets;
  RgBufferId<sh::Material> materials;
  RgBufferId<sh::DirectionalLight> directional_lights;
};

void add_to_draw_set(SceneData &scene, GpuScene &gpu_scene,
                     Handle<MeshInstance> handle, DrawSet set);

void remove_from_draw_set(SceneData &scene, GpuScene &gpu_scene,
                          Handle<MeshInstance> handle, DrawSet set);

auto get_batch_pipeline(DrawSet ds, const BatchDesc &desc,
                        const Pipelines &pipelines) -> Handle<GraphicsPipeline>;

auto get_batch_indices(const BatchDesc &desc, const SceneData &scene)
    -> BufferSlice<u8>;

} // namespace ren
