#include "GpuScene.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"
#include "core/Views.hpp"
#include "ren/ren.hpp"

#include <algorithm>
#include <fmt/format.h>

namespace ren {

auto init_gpu_scene(ResourceArena &arena) -> GpuScene {
  constexpr rhi::MemoryHeap HEAP = rhi::MemoryHeap::Default;

#define create_buffer(T, n, c)                                                 \
  arena                                                                        \
      .create_buffer<T>({                                                      \
          .name = n,                                                           \
          .heap = HEAP,                                                        \
          .count = c,                                                          \
      })                                                                       \
      .value()

  constexpr usize NUM_MESH_INSTANCE_VISIBILITY_MASKS = ceil_div(
      MAX_NUM_MESH_INSTANCES, sh::MESH_INSTANCE_VISIBILITY_MASK_BIT_SIZE);

  GpuScene gpu_scene = {
      .exposure = create_buffer(float, "Exposure", 1),
      .meshes = create_buffer(sh::Mesh, "Scene meshes", MAX_NUM_MESHES),
      .mesh_instances = create_buffer(sh::MeshInstance, "Scene mesh instances",
                                      MAX_NUM_MESH_INSTANCES),
      .mesh_instance_visibility = create_buffer(
          sh::MeshInstanceVisibilityMask, "Scene mesh instance visibility",
          NUM_MESH_INSTANCE_VISIBILITY_MASKS),
      .materials =
          create_buffer(sh::Material, "Scene materials", MAX_NUM_MATERIALS),
      .directional_lights =
          create_buffer(sh::DirectionalLight, "Scene directional lights",
                        MAX_NUM_DIRECTIONAL_LIGHTS),
  };

  for (auto i : range(NUM_DRAW_SETS)) {
    auto s = (DrawSet)(1 << i);
    DrawSetData &ds = gpu_scene.draw_sets[i];
    ds.cull_data = create_buffer(
        sh::InstanceCullData,
        fmt::format("Draw set {} mesh instances", get_draw_set_name(s)),
        MAX_NUM_MESH_INSTANCES);
  }

#undef create_buffer

  return gpu_scene;
}

auto get_batch_desc(const SceneData &scene, const MeshInstance &mesh_instance)
    -> BatchDesc {
  const Mesh &mesh = scene.meshes.get(mesh_instance.mesh);
  const sh::Material &material = scene.materials.get(mesh_instance.material);

  MeshAttributeFlags attributes;
  if (mesh.uvs) {
    attributes |= MeshAttribute::UV;
  }
  if (material.normal_texture) {
    attributes |= MeshAttribute::Tangent;
  }
  if (mesh.colors) {
    attributes |= MeshAttribute::Color;
  }

  return {
      .attributes = attributes,
      .index_pool = mesh.index_pool,
  };
}

void add_to_draw_set(SceneData &scene, GpuScene &gpu_scene,
                     Handle<MeshInstance> handle, DrawSet set) {
  MeshInstance &mesh_instance = scene.mesh_instances[handle];

  u32 ds_idx = get_draw_set_index(set);

  DrawSetData &ds = gpu_scene.draw_sets[ds_idx];

  BatchDesc batch_desc = get_batch_desc(scene, mesh_instance);

  auto it = std::ranges::find_if(
      ds.batches, [&](const Batch &batch) { return batch.desc == batch_desc; });

  Batch *batch;
  [[unlikely]] if (it == ds.batches.end()) {
    batch = &ds.batches.emplace_back(Batch{batch_desc});
  } else {
    batch = &*it;
  }

  const Mesh &mesh = scene.meshes.get(mesh_instance.mesh);
  u32 num_meshlets = mesh.lods[0].num_meshlets;
  batch->num_meshlets += num_meshlets;

  DrawSetId id(ds.mesh_instances.size());
  ds.mesh_instances.push_back(handle);
  ds.update_cull_data.push_back(sh::InstanceCullData{
      .mesh = mesh_instance.mesh,
      .mesh_instance = handle,
      .batch = sh::BatchId(batch - ds.batches.data()),
  });

  ren_assert(not mesh_instance.draw_sets.is_set(set));
  mesh_instance.draw_sets |= set;
  mesh_instance.draw_set_ids[ds_idx] = id;
}

void remove_from_draw_set(SceneData &scene, GpuScene &gpu_scene,
                          Handle<MeshInstance> handle, DrawSet set) {
  MeshInstance &mesh_instance = scene.mesh_instances[handle];

  u32 ds_idx = get_draw_set_index(set);

  DrawSetData &ds = gpu_scene.draw_sets[ds_idx];

  DrawSetId id = mesh_instance.draw_set_ids[ds_idx];
  ren_assert(id != InvalidDrawSetId);
  [[maybe_unused]] usize num_before =
      ds.mesh_instances.size() - ds.update_cull_data.size();
  ren_assert_msg(id < num_before,
                 "Deleting items that were added to a draw set the during the "
                 "same frame is not supported");

  BatchDesc batch_desc = get_batch_desc(scene, mesh_instance);

  auto it = std::ranges::find_if(
      ds.batches, [&](const Batch &batch) { return batch.desc == batch_desc; });

  Batch *batch;
  [[unlikely]] if (it == ds.batches.end()) {
    batch = &ds.batches.emplace_back(Batch{batch_desc});
  } else {
    batch = &*it;
  }

  const Mesh &mesh = scene.meshes.get(mesh_instance.mesh);
  u32 num_meshlets = mesh.lods[0].num_meshlets;
  batch->num_meshlets -= num_meshlets;

  ds.delete_ids.push_back(id);

  ren_assert(mesh_instance.draw_sets.is_set(set));
  mesh_instance.draw_sets.reset(set);
  mesh_instance.draw_set_ids[ds_idx] = InvalidDrawSetId;
}

auto get_batch_pipeline(DrawSet ds, const BatchDesc &desc,
                        const Pipelines &pipelines)
    -> Handle<GraphicsPipeline> {
  switch (ds) {
  case DrawSet::DepthOnly:
    return pipelines.early_z_pass;
  case DrawSet::Opaque:
    return pipelines.opaque_pass[(i32)desc.attributes.get()];
  }
  std::unreachable();
}

auto get_batch_indices(const BatchDesc &desc, const SceneData &scene)
    -> BufferSlice<u8> {
  return BufferSlice<u8>{scene.index_pools[desc.index_pool].indices};
}

} // namespace ren
