#include "GpuScene.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"
#include "Support/Views.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"
#include "glsl/Mesh.h"
#include "ren/ren.hpp"

#include <algorithm>
#include <fmt/format.h>

namespace ren {

auto init_gpu_scene(ResourceArena &arena) -> GpuScene {
  constexpr BufferHeap HEAP = BufferHeap::Static;
  constexpr VkBufferUsageFlags USAGE =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

#define create_buffer(T, n, c)                                                 \
  StatefulBufferSlice<T> {                                                     \
    arena.create_buffer<T>({                                                   \
        .name = n,                                                             \
        .heap = HEAP,                                                          \
        .usage = USAGE,                                                        \
        .count = c,                                                            \
    })                                                                         \
  }

  constexpr usize NUM_MESH_INSTANCE_VISIBILITY_MASKS = ceil_div(
      MAX_NUM_MESH_INSTANCES, glsl::MESH_INSTANCE_VISIBILITY_MASK_BIT_SIZE);

  GpuScene gpu_scene = {
      .meshes = create_buffer(glsl::Mesh, "Scene meshes", MAX_NUM_MESHES),
      .mesh_instances = create_buffer(
          glsl::MeshInstance, "Scene mesh instances", MAX_NUM_MESH_INSTANCES),
      .mesh_instance_visibility = create_buffer(
          MeshInstanceVisibilityMask, "Scene mesh instance visibility",
          NUM_MESH_INSTANCE_VISIBILITY_MASKS),
      .materials =
          create_buffer(glsl::Material, "Scene materials", MAX_NUM_MATERIALS),
      .directional_lights =
          create_buffer(glsl::DirectionalLight, "Scene directional lights",
                        MAX_NUM_DIRECTIONAL_LIGHTS),
  };

  for (auto i : range(NUM_DRAW_SETS)) {
    auto s = (DrawSet)(1 << i);
    DrawSetData &ds = gpu_scene.draw_sets[i];
    ds.cull_data = create_buffer(
        glsl::InstanceCullData,
        fmt::format("Draw set {} mesh instances", get_draw_set_name(s)),
        MAX_NUM_MESH_INSTANCES);
  }

#undef create_buffer

  return gpu_scene;
}

auto get_depth_only_batch_desc(const SceneData &scene,
                               const Pipelines &pipelines,
                               const MeshInstance &mesh_instance) -> BatchDesc {
  const Mesh &mesh = scene.meshes.get(mesh_instance.mesh);
  return {
      .pipeline = pipelines.early_z_pass,
      .index_buffer = scene.index_pools[mesh.index_pool].indices,
  };
}

auto get_opaque_batch_desc(const SceneData &scene, const Pipelines &pipelines,
                           const MeshInstance &mesh_instance) -> BatchDesc {
  const Mesh &mesh = scene.meshes.get(mesh_instance.mesh);
  const Material &material = scene.materials.get(mesh_instance.material);

  MeshAttributeFlags attributes;
  if (material.base_color_texture) {
    attributes |= MeshAttribute::UV;
  }
  if (material.normal_texture) {
    attributes |= MeshAttribute::UV | MeshAttribute::Tangent;
  }
  if (mesh.colors) {
    attributes |= MeshAttribute::Color;
  }

  return {
      .pipeline = pipelines.opaque_pass[i32(attributes.get())],
      .index_buffer = scene.index_pools[mesh.index_pool].indices,
  };
}

auto get_batch_desc(const SceneData &scene, const Pipelines &pipelines,
                    const MeshInstance &mesh_instance, DrawSet draw_set)
    -> BatchDesc {
  switch (draw_set) {
  case DrawSet::DepthOnly:
    return get_depth_only_batch_desc(scene, pipelines, mesh_instance);
  case DrawSet::Opaque:
    return get_opaque_batch_desc(scene, pipelines, mesh_instance);
  }
  std::unreachable();
}

void add_to_draw_set(SceneData &scene, GpuScene &gpu_scene,
                     const Pipelines &pipelines, Handle<MeshInstance> handle,
                     DrawSet set) {
  MeshInstance &mesh_instance = scene.mesh_instances[handle];

  u32 ds_idx = get_draw_set_index(set);

  DrawSetData &ds = gpu_scene.draw_sets[ds_idx];

  BatchDesc batch_desc = get_batch_desc(scene, pipelines, mesh_instance, set);

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
  ds.update_cull_data.push_back(glsl::InstanceCullData{
      .mesh = mesh_instance.mesh,
      .mesh_instance = handle,
      .batch = BatchId(batch - ds.batches.data()),
  });

  ren_assert(not mesh_instance.draw_sets.is_set(set));
  mesh_instance.draw_sets |= set;
  mesh_instance.draw_set_ids[ds_idx] = id;
}

void remove_from_draw_set(SceneData &scene, GpuScene &gpu_scene,
                          const Pipelines &pipelines,
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

  BatchDesc batch_desc = get_batch_desc(scene, pipelines, mesh_instance, set);

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

} // namespace ren
