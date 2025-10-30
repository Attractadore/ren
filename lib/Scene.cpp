#include "Scene.hpp"
#include "CommandRecorder.hpp"
#include "Formats.hpp"
#include "SwapChain.hpp"
#include "passes/HiZ.hpp"
#include "passes/ImGui.hpp"
#include "passes/MeshPass.hpp"
#include "passes/Opaque.hpp"
#include "passes/PostProcessing.hpp"
#include "passes/Present.hpp"
#include "passes/Skybox.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Span.hpp"
#include "ren/ren.hpp"

#include "Ssao.comp.hpp"
#include "SsaoFilter.comp.hpp"
#include "SsaoHiZ.comp.hpp"
#include "sh/Random.h"

#include <algorithm>
#include <ktx.h>
#include <tracy/Tracy.hpp>

namespace ren {

GpuScene GpuScene::init(NotNull<ResourceArena *> arena) {
  constexpr rhi::MemoryHeap HEAP = rhi::MemoryHeap::Default;

#define create_buffer(T, n, c)                                                 \
  arena                                                                        \
      ->create_buffer<T>({                                                     \
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
  };

  ScratchArena scratch;

  for (auto i : range(NUM_DRAW_SETS)) {
    auto s = (DrawSet)(1 << i);
    DrawSetData &ds = gpu_scene.draw_sets[i];
    ds.data = arena
                  ->create_buffer<sh ::DrawSetItem>(

                      {
                          .name = format(scratch, "Draw set {} mesh instances",
                                         get_draw_set_name(s)),
                          .heap = HEAP,
                          .count = MAX_NUM_MESH_INSTANCES,
                      })
                  .value();
  }

#undef create_buffer

  return gpu_scene;
}

} // namespace ren

namespace ren_export {

namespace {

Result<void, Error> init_internal_data(NotNull<Scene *> scene) {
  ScratchArena scratch({&scene->m_internal_arena});

  scene->m_sid = scene->m_internal_arena.allocate<SceneInternalData>();
  auto *id = scene->m_sid;

  Renderer *renderer = scene->m_renderer;

  id->m_rcs_arena =
      ResourceArena::init(&scene->m_internal_arena, scene->m_renderer);
  ren_try(id->m_pipelines, load_pipelines(id->m_rcs_arena));

  id->m_gfx_allocator =
      DeviceBumpAllocator::init(*renderer, id->m_rcs_arena, 256 * MiB);
  if (renderer->is_queue_family_supported(rhi::QueueFamily::Compute)) {
    id->m_async_allocator =
        DeviceBumpAllocator::init(*renderer, id->m_rcs_arena, 16 * MiB);
    for (DeviceBumpAllocator &allocator : id->m_shared_allocators) {
      allocator =
          DeviceBumpAllocator::init(*renderer, id->m_rcs_arena, 16 * MiB);
    }
  }

  for (auto i : range(NUM_FRAMES_IN_FLIGHT)) {
    FrameResources &frcs = id->m_per_frame_resources[i];
    ren_try(frcs.acquire_semaphore,
            id->m_rcs_arena.create_semaphore({
                .name = format(scratch, "Acquire semaphore {}", i),
                .type = rhi::SemaphoreType::Binary,
            }));
    ren_try(frcs.gfx_cmd_pool,
            id->m_rcs_arena.create_command_pool({
                .name = format(scratch, "Command pool {}", i),
                .queue_family = rhi::QueueFamily::Graphics,
            }));
    if (renderer->is_queue_family_supported(rhi::QueueFamily::Compute)) {
      ren_try(frcs.async_cmd_pool,
              id->m_rcs_arena.create_command_pool({
                  .name = format(scratch, "Command pool {}", i),
                  .queue_family = rhi::QueueFamily::Compute,
              }));
    }
    frcs.upload_allocator =
        UploadBumpAllocator::init(*renderer, id->m_rcs_arena, 128 * MiB);
    frcs.descriptor_allocator =
        DescriptorAllocatorScope::init(&scene->m_descriptor_allocator);
  }

  id->m_rgp = RgPersistent::init(&scene->m_rg_arena, renderer);

  return {};
}

void destroy_internal_data(NotNull<Scene *> scene) {
  scene->m_sid->m_rcs_arena.clear();
  scene->m_sid->m_rgp.destroy();
}

void next_frame(NotNull<Scene *> scene) {
  ZoneScoped;

  Renderer *renderer = scene->m_renderer;
  SceneInternalData *id = scene->m_sid;
  scene->m_frcs =
      &id->m_per_frame_resources[scene->m_frame_index % NUM_FRAMES_IN_FLIGHT];
  FrameResources *frcs = scene->m_frcs;

  {
    ZoneScopedN("Scene::wait_for_previous_frame");
    if (renderer->try_get_semaphore(frcs->end_semaphore)) {
      std::ignore =
          renderer->wait_for_semaphore(frcs->end_semaphore, frcs->end_time);
    }
  }

  frcs->upload_allocator.reset();
  std::ignore = renderer->reset_command_pool(frcs->gfx_cmd_pool);
  if (frcs->async_cmd_pool) {
    std::ignore = renderer->reset_command_pool(frcs->async_cmd_pool);
  }
  frcs->descriptor_allocator.reset();
  frcs->end_semaphore = {};
  frcs->end_time = 0;

  id->m_gfx_allocator.reset();
  CommandRecorder cmd;
  std::ignore = cmd.begin(*renderer, frcs->gfx_cmd_pool);
  {
    auto _ = cmd.debug_region("begin-frame");
    cmd.memory_barrier(rhi::ALL_COMMANDS_BARRIER);
  }
  rhi::CommandBuffer cmd_buffer = cmd.end().value();
  std::ignore = renderer->submit(rhi::QueueFamily::Graphics, {cmd_buffer});

  if (scene->m_settings.async_compute) {
    id->m_async_allocator.reset();
    std::swap(id->m_shared_allocators[0], id->m_shared_allocators[1]);
    id->m_shared_allocators[0].reset();
    CommandRecorder cmd;
    std::ignore = cmd.begin(*renderer, frcs->async_cmd_pool);
    {
      auto _ = cmd.debug_region("begin-frame");
      cmd.memory_barrier(rhi::ALL_COMMANDS_BARRIER);
    }
    rhi::CommandBuffer cmd_buffer = cmd.end().value();
    std::ignore = renderer->submit(rhi::QueueFamily::Compute, {cmd_buffer});
  }

  scene->m_sid->m_transform_staging_buffers = {};
  scene->m_gpu_scene_update = {};
  scene->m_sid->m_resource_uploader = {};
}

sh::Handle<sh::Sampler2D> get_or_create_texture(NotNull<Scene *> scene,
                                                Handle<Image> image,
                                                const SamplerDesc &sampler) {
  return scene->m_descriptor_allocator.allocate_sampled_texture<sh::Sampler2D>(
      *scene->m_renderer, SrvDesc{scene->m_images[image].handle},
      {
          .mag_filter = get_rhi_Filter(sampler.mag_filter),
          .min_filter = get_rhi_Filter(sampler.min_filter),
          .mipmap_mode = get_rhi_SamplerMipmapMode(sampler.mipmap_filter),
          .address_mode_u = get_rhi_SamplerAddressMode(sampler.wrap_u),
          .address_mode_v = get_rhi_SamplerAddressMode(sampler.wrap_v),
          .max_anisotropy = 16.0f,
      });
}

auto create_texture(NotNull<Arena *> frame_arena, NotNull<Scene *> scene,
                    const void *blob, usize size) -> expected<Handle<Texture>> {
  ktx_error_code_e err = KTX_SUCCESS;
  ktxTexture2 *ktx_texture2 = nullptr;
  err = ktxTexture2_CreateFromMemory((const u8 *)blob, size, 0, &ktx_texture2);
  if (err) {
    return Failure(Error::Unknown);
  }
  auto res = scene->m_sid->m_resource_uploader.create_texture(
      frame_arena, scene->m_rcs_arena, scene->m_frcs->upload_allocator,
      ktx_texture2);
  ktxTexture_Destroy(ktxTexture(ktx_texture2));
  return res;
}

bool is_amd_anti_lag_available(NotNull<Scene *> scene) {
  return scene->m_renderer->is_feature_supported(RendererFeature::AmdAntiLag);
}

bool is_amd_anti_lag_enabled(NotNull<Scene *> scene) {
  return is_amd_anti_lag_available(scene) and scene->m_settings.amd_anti_lag;
}

} // namespace

auto create_scene(NotNull<Arena *> arena, Renderer *renderer,
                  SwapChain *swap_chain) -> expected<Scene *> {
  auto *scene = new Scene{
      .m_arena = arena,
      .m_internal_arena = make_arena(),
      .m_rg_arena = make_arena(),
      .m_renderer = renderer,
      .m_swap_chain = swap_chain,
      .m_descriptor_allocator = DescriptorAllocator::init(arena),
      .m_cameras = GenArray<Camera>::init(arena),
      .m_meshes = GenArray<Mesh>::init(arena),
      .m_mesh_instances = GenArray<MeshInstance>::init(arena),
      .m_images = GenArray<Image>::init(arena),
      .m_materials = GenArray<Material>::init(arena),
      .m_directional_lights = GenArray<DirectionalLight>::init(arena),
  };

  scene->m_rcs_arena = ResourceArena::init(arena, renderer);
  scene->m_gpu_scene = GpuScene::init(&scene->m_rcs_arena);

  scene->m_settings.async_compute =
      renderer->is_queue_family_supported(rhi::QueueFamily::Compute);
  scene->m_settings.present_from_compute =
      swap_chain->is_queue_family_supported(rhi::QueueFamily::Compute);

  scene->m_settings.amd_anti_lag =
      renderer->is_feature_supported(RendererFeature::AmdAntiLag);

  ren_try_to(init_internal_data(scene));
  next_frame(scene);

  return scene;
}

void destroy_scene(Scene *scene) {
  if (!scene) {
    return;
  }
  scene->m_renderer->wait_idle();
  scene->m_rcs_arena.clear();
  destroy_internal_data(scene);
  scene->m_internal_arena.destroy();
  scene->m_rg_arena.destroy();
  delete scene;
}

Handle<Mesh> create_mesh(NotNull<Arena *> frame_arena, Scene *scene,
                         std::span<const std::byte> blob) {
  ScratchArena scratch(frame_arena);

  if (blob.size() < sizeof(MeshPackageHeader)) {
    return NullHandle;
  }

  const auto &header = *(const MeshPackageHeader *)blob.data();
  if (header.magic != MESH_PACKAGE_MAGIC) {
    return NullHandle;
  }
  if (header.version != MESH_PACKAGE_VERSION) {
    return NullHandle;
  }

  Span positions = {
      (const sh::Position *)&blob[header.positions_offset],
      header.num_vertices,
  };

  Span normals = {
      (const sh::Normal *)&blob[header.normals_offset],
      header.num_vertices,
  };

  Span tangents = {
      (const sh::Tangent *)&blob[header.tangents_offset],
      header.tangents_offset ? header.num_vertices : 0,
  };

  Span uvs = {
      (const sh::UV *)&blob[header.uvs_offset],
      header.uvs_offset ? header.num_vertices : 0,
  };

  Span colors = {
      (const sh::Color *)&blob[header.colors_offset],
      header.colors_offset ? header.num_vertices : 0,
  };

  Span indices = {
      (const u32 *)&blob[header.indices_offset],
      header.num_indices,
  };

  // Create a copy because we need to patch base triangle indices.
  auto meshlets = Span<sh::Meshlet>::allocate(scratch, header.num_meshlets);
  copy((const sh::Meshlet *)&blob[header.meshlets_offset], header.num_meshlets,
       meshlets.data());

  Span triangles = {
      (const u8 *)&blob[header.triangles_offset],
      header.num_triangles * 3,
  };

  Mesh mesh = {
      .bb = header.bb,
      .scale = header.scale,
      .uv_bs = header.uv_bs,
      .num_lods = header.num_lods,
  };
  copy(header.lods, header.num_lods, mesh.lods);

  // Upload vertices

  Renderer *renderer = scene->m_renderer;

  auto upload_buffer = [&]<typename T>(Span<const T> data,
                                       Handle<Buffer> &buffer,
                                       String8 name) -> Result<void, Error> {
    if (not data.empty()) {
      ren_try(BufferSlice<T> slice, scene->m_rcs_arena.create_buffer<T>({
                                        .name = std::move(name),
                                        .heap = rhi::MemoryHeap::Default,
                                        .count = data.size(),
                                    }));
      buffer = slice.buffer;
      scene->m_sid->m_resource_uploader.stage_buffer(
          frame_arena, *renderer, scene->m_frcs->upload_allocator, Span(data),
          slice);
    }
    return {};
  };

  u32 index = scene->m_meshes.size();

  if (!upload_buffer(positions, mesh.positions,
                     format(scratch, "Mesh {} positions", index))) {
    return NullHandle;
  }
  if (!upload_buffer(normals, mesh.normals,
                     format(scratch, "Mesh {} normals", index))) {
    return NullHandle;
  }
  if (!upload_buffer(tangents, mesh.tangents,
                     format(scratch, "Mesh {} tangents", index))) {
    return NullHandle;
  }
  if (!upload_buffer(uvs, mesh.uvs, format(scratch, "Mesh {} uvs", index))) {
    return NullHandle;
  }
  if (!upload_buffer(colors, mesh.colors,
                     format(scratch, "Mesh {} colors", index))) {
    return NullHandle;
  }

  // Find or allocate index pool

  ren_assert_msg(header.num_triangles * 3 <= sh::INDEX_POOL_SIZE,
                 "Index pool overflow");

  if (scene->m_index_pools.m_size == 0 or
      scene->m_index_pools.back().num_free_indices < header.num_triangles * 3) {
    expected<BufferSlice<u8>> slice = scene->m_rcs_arena.create_buffer<u8>({
        .name = "Mesh vertex indices pool",
        .heap = rhi::MemoryHeap::Default,
        .count = sh::INDEX_POOL_SIZE,
    });
    if (!slice) {
      return NullHandle;
    }
    scene->m_index_pools.push(scene->m_arena,
                              {slice->buffer, sh::INDEX_POOL_SIZE});
  }

  mesh.index_pool = scene->m_index_pools.m_size - 1;
  IndexPool &index_pool = scene->m_index_pools.back();

  u32 base_triangle = sh::INDEX_POOL_SIZE - index_pool.num_free_indices;
  for (sh::Meshlet &meshlet : meshlets) {
    meshlet.base_triangle += base_triangle;
  }

  index_pool.num_free_indices -= header.num_triangles * 3;

  if (!upload_buffer(indices, mesh.indices,
                     format(scratch, "Mesh {} indices", index))) {
    return NullHandle;
  }

  // Upload meshlets

  if (!upload_buffer(Span<const sh::Meshlet>(meshlets), mesh.meshlets,
                     format(scratch, "Mesh {} meshlets", index))) {
    return NullHandle;
  }

  // Upload triangles

  scene->m_sid->m_resource_uploader.stage_buffer(
      frame_arena, *renderer, scene->m_frcs->upload_allocator, triangles,
      renderer->get_buffer_slice<u8>(index_pool.indices)
          .slice(base_triangle, header.num_triangles * 3));

  Handle<Mesh> handle = scene->m_meshes.insert(scene->m_arena, mesh);

  sh::Mesh gpu_mesh = {
      .positions =
          renderer->get_buffer_device_ptr<sh::Position>(mesh.positions),
      .normals = renderer->get_buffer_device_ptr<sh::Normal>(mesh.normals),
      .tangents =
          renderer->try_get_buffer_device_ptr<sh::Tangent>(mesh.tangents),
      .uvs = renderer->try_get_buffer_device_ptr<sh::UV>(mesh.uvs),
      .colors = renderer->try_get_buffer_device_ptr<sh::Color>(mesh.colors),
      .meshlets = renderer->get_buffer_device_ptr<sh::Meshlet>(mesh.meshlets),
      .meshlet_indices = renderer->get_buffer_device_ptr<u32>(mesh.indices),
      .bb = mesh.bb,
      .uv_bs = mesh.uv_bs,
      .index_pool = mesh.index_pool,
      .num_lods = mesh.num_lods,
  };
  copy(mesh.lods, mesh.num_lods, gpu_mesh.lods);

  scene->m_gpu_scene_update.meshes.push(frame_arena, {handle, gpu_mesh});

  return handle;
}

Handle<Image> create_image(NotNull<Arena *> frame_arena, Scene *scene,
                           std::span<const std::byte> blob) {
  expected<Handle<Texture>> texture =
      create_texture(frame_arena, scene, blob.data(), blob.size());
  if (!texture) {
    return NullHandle;
  }
  return scene->m_images.insert(scene->m_arena, {*texture});
}

Handle<Material> create_material(NotNull<Arena *> frame_arena, Scene *scene,
                                 const MaterialCreateInfo &info) {
  auto get_descriptor = [&](const auto &texture) -> sh::Handle<sh::Sampler2D> {
    if (texture.image) {
      return get_or_create_texture(scene, texture.image, texture.sampler);
    }
    return {};
  };

  sh::Material gpu_material = {
      .base_color = info.base_color_factor,
      .base_color_texture = get_descriptor(info.base_color_texture),
      .occlusion_strength = info.orm_texture.strength,
      .roughness = info.roughness_factor,
      .metallic = info.metallic_factor,
      .orm_texture = get_descriptor(info.orm_texture),
      .normal_scale = info.normal_texture.scale,
      .normal_texture = get_descriptor(info.normal_texture),
  };

  Handle<Material> handle =
      scene->m_materials.insert(scene->m_arena, {gpu_material});

  scene->m_gpu_scene_update.materials.push(frame_arena, {handle, gpu_material});

  return handle;
}

Handle<Camera> create_camera(Scene *scene) {
  return scene->m_cameras.insert(scene->m_arena);
}

void destroy_camera(Scene *scene, Handle<Camera> camera) {
  scene->m_cameras.erase(camera);
}

void set_camera(Scene *scene, Handle<Camera> camera) {
  scene->m_camera = camera;
}

void set_camera_perspective_projection(
    Scene *scene, Handle<Camera> handle,
    const CameraPerspectiveProjectionDesc &desc) {
  Camera &camera = scene->m_cameras[handle];
  camera.proj = CameraProjection::Perspective;
  camera.persp_hfov = desc.hfov;
  camera.near = desc.near;
  camera.far = 0.0f;
}

void set_camera_orthographic_projection(
    Scene *scene, Handle<Camera> handle,
    const CameraOrthographicProjectionDesc &desc) {
  Camera &camera = scene->m_cameras[handle];
  camera.proj = CameraProjection::Orthograpic;
  camera.ortho_width = desc.width;
  camera.near = desc.near;
  camera.far = desc.far;
}

void set_camera_transform(Scene *scene, Handle<Camera> handle,
                          const CameraTransformDesc &desc) {
  Camera &camera = scene->m_cameras[handle];
  camera.position = desc.position;
  camera.forward = desc.forward;
  camera.up = desc.up;
}

auto get_batch_desc(DrawSet ds, const Scene &scene,
                    const MeshInstance &mesh_instance) -> DrawSetBatchDesc {
  const Mesh &mesh = scene.m_meshes.get(mesh_instance.mesh);
  const IndexPool &pool = scene.m_index_pools[mesh.index_pool];

  BufferSlice<u8> indices = {
      .buffer = pool.indices,
      .count = sh::INDEX_POOL_SIZE,
  };

  switch (ds) {
  case DrawSet::DepthOnly:
    return {scene.m_sid->m_pipelines.early_z_pass, indices};
  case DrawSet::Opaque: {
    const sh::Material &material =
        scene.m_materials[mesh_instance.material].data;

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
    return {scene.m_sid->m_pipelines.opaque_pass[(i32)attributes.get()],
            indices};
  };
  }

  std::unreachable();
}

void create_mesh_instances(NotNull<Arena *> frame_arena, Scene *scene,
                           std::span<const MeshInstanceCreateInfo> create_info,
                           std::span<Handle<MeshInstance>> out) {
  ren_assert(out.size() >= create_info.size());
  ren_assert(scene->m_mesh_instances.size() + create_info.size() <=
             MAX_NUM_MESH_INSTANCES);

  for (usize i : range(create_info.size())) {
    ren_assert(create_info[i].mesh);
    ren_assert(create_info[i].material);

    Handle<MeshInstance> handle = scene->m_mesh_instances.insert(
        scene->m_arena, {create_info[i].mesh, create_info[i].material});
    MeshInstance &mesh_instance = scene->m_mesh_instances[handle];

    const Mesh &mesh = scene->m_meshes[create_info[i].mesh];
    u32 num_meshlets = mesh.lods[0].num_meshlets;

    for (auto k : range(NUM_DRAW_SETS)) {
      DrawSetData &ds = scene->m_gpu_scene.draw_sets[k];

      DrawSetBatchDesc batch_desc =
          get_batch_desc((DrawSet)(1 << k), *scene, mesh_instance);
      DrawSetBatch *batch = nullptr;
      for (DrawSetBatch &b : ds.batches) {
        if (b.desc == batch_desc) {
          batch = &b;
          break;
        }
      }
      if (!batch) {
        ds.batches.push(scene->m_arena, {batch_desc});
        batch = &ds.batches.back();
      }
      batch->num_meshlets += num_meshlets;

      DrawSetId id(ds.items.m_size);
      ds.items.push(scene->m_arena, handle);
      mesh_instance.draw_set_ids[k] = id;

      sh::DrawSetItem gpu_item = {
          .mesh = mesh_instance.mesh,
          .mesh_instance = handle,
          .batch = sh::BatchId(batch - ds.batches.m_data),
      };
      scene->m_gpu_scene_update.draw_sets[k].update.push(frame_arena,
                                                         {id, gpu_item});
    }

    scene->m_gpu_scene_update.mesh_instances.push(
        frame_arena, {
                         handle,
                         {
                             .mesh = mesh_instance.mesh,
                             .material = mesh_instance.material,
                         },
                     });

    out[i] = handle;
  }
}

void destroy_mesh_instances(
    NotNull<Arena *> frame_arena, Scene *scene,
    std::span<const Handle<MeshInstance>> mesh_instances) {
  for (Handle<MeshInstance> handle : mesh_instances) {
    if (!handle) {
      continue;
    }

    const MeshInstance &mesh_instance = scene->m_mesh_instances[handle];
    const Mesh &mesh = scene->m_meshes.get(mesh_instance.mesh);
    u32 num_meshlets = mesh.lods[0].num_meshlets;

    for (auto k : range(NUM_DRAW_SETS)) {
      DrawSetData &ds = scene->m_gpu_scene.draw_sets[k];

      DrawSetBatchDesc batch_desc =
          get_batch_desc((DrawSet)(1 << k), *scene, mesh_instance);
      DrawSetBatch *batch = nullptr;
      for (DrawSetBatch &b : ds.batches) {
        if (b.desc == batch_desc) {
          batch = &b;
          break;
        }
      }
      ren_assert(batch);
      batch->num_meshlets -= num_meshlets;

      DrawSetId id = mesh_instance.draw_set_ids[k];
      ren_assert(id != InvalidDrawSetId);
      scene->m_gpu_scene_update.draw_sets[k].remove.push(frame_arena, id);
    }

    scene->m_mesh_instances.erase(handle);
  }
}

struct StagingBufferIndexAndOffset {
  usize index = 0;
  usize offset = 0;
};

// If base size is 1, then
// Index 0b000 maps to 0,
// Indices 0b001 and 0b010 map to 1,
// Indices 0b011, 0b100, 0b101, 0b110 map to 2.
// Notice that the staging buffer index is equal to the MSB position of (index
// + 1), and the offset into the staging buffer is all the bits except the
// MSB.
StagingBufferIndexAndOffset mesh_instance_index_to_sb_and_offset(usize index) {
  index = index + MIN_TRANSFORM_STAGING_BUFFER_SIZE;
  usize hi_bit = find_msb(index);
  usize mask = (1 << hi_bit) - 1;
  return {hi_bit - find_msb(MIN_TRANSFORM_STAGING_BUFFER_SIZE), index & mask};
};

void set_mesh_instance_transforms(
    NotNull<Arena *> frame_arena, Scene *scene,
    std::span<const Handle<MeshInstance>> mesh_instances,
    std::span<const glm::mat4x3> matrices) {
  ZoneScoped;

  ren_assert(mesh_instances.size() == matrices.size());

  usize raw_size = scene->m_mesh_instances.raw_size();
  usize num_sbs = mesh_instance_index_to_sb_and_offset(raw_size - 1).index + 1;
  for (usize i : range<usize>(scene->m_sid->m_transform_staging_buffers.m_size,
                              num_sbs)) {
    usize size = MIN_TRANSFORM_STAGING_BUFFER_SIZE << i;
    scene->m_sid->m_transform_staging_buffers.push(
        frame_arena,
        scene->m_frcs->upload_allocator.allocate<glm::mat4x3>(size));
  }

  for (usize i : range(mesh_instances.size())) {
    Handle<MeshInstance> handle = mesh_instances[i];
    const MeshInstance &mesh_instance = scene->m_mesh_instances[handle];
    const Mesh &mesh = scene->m_meshes[mesh_instance.mesh];
    auto [sb, offset] = mesh_instance_index_to_sb_and_offset(handle);
    scene->m_sid->m_transform_staging_buffers[sb].host_ptr[offset] =
        matrices[i] * sh::make_decode_position_matrix(mesh.scale);
  }
}

Handle<DirectionalLight>
create_directional_light(Scene *scene, const DirectionalLightDesc &desc) {
  Handle<DirectionalLight> handle =
      scene->m_directional_lights.insert(scene->m_arena);
  ren_export::set_directional_light(scene, handle, desc);
  return handle;
};

void destroy_directional_light(Scene *scene, Handle<DirectionalLight> handle) {
  scene->m_directional_lights.erase(handle);
}

void set_directional_light(Scene *scene, Handle<DirectionalLight> handle,
                           const DirectionalLightDesc &desc) {
  sh::DirectionalLight gpu_light = {
      .color = desc.color,
      .illuminance = desc.illuminance,
      .origin = glm::normalize(desc.origin),
  };
  scene->m_directional_lights[handle] = {gpu_light};
};

void set_environment_color(Scene *scene, const glm::vec3 &luminance) {
  scene->m_environment_luminance = luminance;
}

auto set_environment_map(Scene *scene, Handle<Image> image) -> expected<void> {
  if (!image) {
    scene->m_environment_map = {};
    return {};
  }
  scene->m_environment_map =
      scene->m_descriptor_allocator.allocate_sampled_texture<sh::SamplerCube>(
          *scene->m_renderer, SrvDesc{scene->m_images[image].handle},
          {
              .mag_filter = rhi::Filter::Linear,
              .min_filter = rhi::Filter::Linear,
              .mipmap_mode = rhi::SamplerMipmapMode::Linear,
          });
  return {};
}

auto delay_input(Scene *scene) -> expected<void> {
  if (is_amd_anti_lag_enabled(scene)) {
    return scene->m_renderer->amd_anti_lag_input(scene->m_frame_index);
  }
  return {};
}

RgGpuScene gpu_scene_update_pass(NotNull<Scene *> scene,
                                 const PassCommonConfig &cfg) {
  ScratchArena scratch(cfg.rgb->m_arena);

  for (usize s : range(NUM_DRAW_SETS)) {
    DrawSetData &data = scene->m_gpu_scene.draw_sets[s];
    DrawSetUpdate &update = scene->m_gpu_scene_update.draw_sets[s];
    std::ranges::sort(update.remove);
    for (isize i = isize(update.remove.m_size) - 1; i >= 0; --i) {
      DrawSetId id = update.remove[i];
      if (id == data.items.m_size - 1) {
        data.items.pop();
        continue;
      }

      std::swap(data.items.back(), data.items[id]);
      data.items.pop();

      Handle<MeshInstance> handle = data.items[id];
      MeshInstance &mesh_instance = scene->m_mesh_instances[handle];
      mesh_instance.draw_set_ids[s] = id;

      DrawSetBatchDesc batch_desc =
          get_batch_desc((DrawSet)(1 << s), *scene, mesh_instance);
      DrawSetBatch *batch = nullptr;
      for (DrawSetBatch &b : data.batches) {
        if (b.desc == batch_desc) {
          batch = &b;
          break;
        }
      }
      ren_assert(batch);

      sh::DrawSetItem gpu_item = {
          .mesh = mesh_instance.mesh,
          .mesh_instance = handle,
          .batch = (sh::BatchId)(batch - data.batches.m_data),
      };
      update.overwrite.push(cfg.rgb->m_arena, {id, gpu_item});
    }
    update.remove.clear();
  }

  RgBuilder &rgb = *cfg.rgb;

  RgGpuScene rg_gpu_scene = {
      .exposure = rgb.create_buffer("exposure", scene->m_gpu_scene.exposure),
      .meshes = rgb.create_buffer("meshes", scene->m_gpu_scene.meshes),
      .mesh_instances = rgb.create_buffer("mesh-instances",
                                          scene->m_gpu_scene.mesh_instances),
      .transform_matrices =
          rgb.create_buffer<glm::mat4x3>({.count = MAX_NUM_MESH_INSTANCES}),
      .mesh_instance_visibility =
          rgb.create_buffer("mesh-instance-visibility",
                            scene->m_gpu_scene.mesh_instance_visibility),
      .materials = rgb.create_buffer("materials", scene->m_gpu_scene.materials),
  };

  for (auto i : range(NUM_DRAW_SETS)) {
    const DrawSetData &ds = scene->m_gpu_scene.draw_sets[i];
    rg_gpu_scene.draw_sets[i] = {
        .items = rgb.create_buffer(format(scratch, "{}-draw-set",
                                          get_draw_set_name((DrawSet)(1 << i))),
                                   ds.data),
    };
  }

  auto pass = rgb.create_pass({"gpu-scene-update"});

  struct {
    const Scene *scene = nullptr;
    RgBufferToken<sh::Mesh> meshes;
    RgBufferToken<sh::MeshInstance> mesh_instances;
    std::array<RgBufferToken<sh::DrawSetItem>, NUM_DRAW_SETS> draw_sets;
    RgBufferToken<glm::mat4x3> transform_matrices;
    RgBufferToken<sh::Material> materials;
    RgBufferToken<sh::DirectionalLight> directional_lights;
  } rcs;

  rcs.scene = scene;

  if (scene->m_gpu_scene_update.meshes.m_size > 0) {
    rcs.meshes = pass.write_buffer("meshes-updated", &rg_gpu_scene.meshes,
                                   rhi::TRANSFER_DST_BUFFER);
  }

  if (scene->m_gpu_scene_update.mesh_instances.m_size > 0) {
    rcs.mesh_instances = pass.write_buffer("mesh-instances-updated",
                                           &rg_gpu_scene.mesh_instances,
                                           rhi::TRANSFER_DST_BUFFER);
  }

  for (auto i : range(NUM_DRAW_SETS)) {
    const DrawSetUpdate &ds = scene->m_gpu_scene_update.draw_sets[i];
    if (ds.update.m_size > 0 or ds.overwrite.m_size > 0) {
      rcs.draw_sets[i] = pass.write_buffer(
          format(scratch, "{}-draw-set-updated",
                 get_draw_set_name((DrawSet)(1 << i))),
          &rg_gpu_scene.draw_sets[i].items,
          rhi::TRANSFER_SRC_BUFFER | rhi::TRANSFER_DST_BUFFER);
    }
  }

  rcs.transform_matrices =
      pass.write_buffer("transform-matrices", &rg_gpu_scene.transform_matrices,
                        rhi::TRANSFER_DST_BUFFER);

  if (scene->m_gpu_scene_update.materials.m_size > 0) {
    rcs.materials = pass.write_buffer(
        "materials-updated", &rg_gpu_scene.materials, rhi::TRANSFER_DST_BUFFER);
  }

  rg_gpu_scene.directional_lights =
      cfg.rgb->create_buffer<sh::DirectionalLight>({
          .name = "directional-lights",
          .count = scene->m_directional_lights.raw_size(),
      });
  rcs.directional_lights = pass.write_buffer("directional-lights-updated",
                                             &rg_gpu_scene.directional_lights,
                                             rhi::TRANSFER_DST_BUFFER);

  pass.set_callback([rcs](Renderer &renderer, const RgRuntime &rg,
                          CommandRecorder &cmd) {
    const GpuSceneUpdate *gsu = &rcs.scene->m_gpu_scene_update;

    if (rcs.meshes) {
      auto _ = cmd.debug_region("Update meshes");
      ZoneScopedN("Update meshes");
      auto data = rg.allocate<sh::Mesh>(gsu->meshes.m_size);
      BufferSlice<sh::Mesh> meshes = rg.get_buffer(rcs.meshes);
      for (usize i : range(gsu->meshes.m_size)) {
        data.host_ptr[i] = gsu->meshes[i].data;
        cmd.copy_buffer(data.slice.slice(i, 1),
                        meshes.slice(gsu->meshes[i].handle, 1));
      }
    }

    if (rcs.mesh_instances) {
      auto _ = cmd.debug_region("Update mesh instances");
      ZoneScopedN("Update mesh instances");
      auto data = rg.allocate<sh::MeshInstance>(gsu->mesh_instances.m_size);
      BufferSlice<sh::MeshInstance> mesh_instances =
          rg.get_buffer(rcs.mesh_instances);
      for (usize i : range(gsu->mesh_instances.m_size)) {
        data.host_ptr[i] = gsu->mesh_instances[i].data;
        cmd.copy_buffer(data.slice.slice(i, 1),
                        mesh_instances.slice(gsu->mesh_instances[i].handle, 1));
      }
    }

    {
      auto _ = cmd.debug_region("Update mesh instance transforms");
      ZoneScopedN("Update mesh instance transforms");
      Span<const UploadBumpAllocation<glm::mat4x3>> sbs =
          rcs.scene->m_sid->m_transform_staging_buffers;
      usize count = rcs.scene->m_mesh_instances.raw_size();
      usize offset = 0;
      for (usize sb : range(sbs.size())) {
        usize sb_size = MIN_TRANSFORM_STAGING_BUFFER_SIZE << sb;

        usize num_copy = min(sb_size, count);
        BufferSlice<glm::mat4x3> src = sbs[sb].slice.slice(0, num_copy);
        BufferSlice<glm::mat4x3> dst =
            rg.get_buffer(rcs.transform_matrices).slice(offset, num_copy);
        cmd.copy_buffer(src, dst);

        count -= num_copy;
        offset += sb_size;
      }
    }

    for (auto s : range(NUM_DRAW_SETS)) {
      if (!rcs.draw_sets[s]) {
        continue;
      }
      const DrawSetUpdate &ds = gsu->draw_sets[s];

      ScratchArena scratch;
      auto _ = cmd.debug_region(format(scratch, "Update draw set {}", s));

      if (ds.update.m_size) {
        auto data = rg.allocate<sh::DrawSetItem>(ds.update.m_size);
        for (usize i : range(ds.update.m_size)) {
          data.host_ptr[i] = ds.update[i].data;
          cmd.copy_buffer(
              data.slice.slice(i, 1),
              rg.get_buffer(rcs.draw_sets[s]).slice(ds.update[i].id));
        }
      }

      if (ds.update.m_size > 0 and ds.overwrite.m_size > 0) {
        cmd.memory_barrier({
            .src_stage_mask = rhi::PipelineStage::Transfer,
            .src_access_mask = rhi::Access::TransferWrite,
            .dst_stage_mask = rhi::PipelineStage::Transfer,
            .dst_access_mask = rhi::Access::TransferWrite,
        });
      }

      if (ds.overwrite.m_size) {
        auto data = rg.allocate<sh::DrawSetItem>(ds.overwrite.m_size);
        for (usize i : range(ds.overwrite.m_size)) {
          data.host_ptr[i] = ds.overwrite[i].data;
          cmd.copy_buffer(
              data.slice.slice(i, 1),
              rg.get_buffer(rcs.draw_sets[s]).slice(ds.overwrite[i].id));
        }
      }
    }

    if (rcs.materials) {
      auto _ = cmd.debug_region("Update materials");
      ZoneScopedN("Update materials");
      auto data = rg.allocate<sh::Material>(gsu->materials.m_size);
      BufferSlice<sh::Material> materials = rg.get_buffer(rcs.materials);
      for (auto i : range(gsu->materials.m_size)) {
        data.host_ptr[i] = gsu->materials[i].data;
        cmd.copy_buffer(data.slice.slice(i, 1),
                        materials.slice(gsu->materials[i].handle, 1));
      }
    }

    if (rcs.directional_lights) {
      auto _ = cmd.debug_region("Update directional lights");
      ZoneScopedN("Update directional lights");

      ScratchArena scratch;
      usize num_lights = rcs.scene->m_directional_lights.size();
      if (num_lights > 0) {
        DynamicArray<sh::DirectionalLight> packed;
        for (auto &&[_, light] : rcs.scene->m_directional_lights) {
          packed.push(scratch, light.data);
        }
        auto data = rg.allocate<sh::DirectionalLight>(packed.m_size);
        copy(Span(packed), data.host_ptr);
        cmd.copy_buffer(data.slice, rg.get_buffer(rcs.directional_lights));
      }
    }
  });

  return rg_gpu_scene;
}

auto build_rg(NotNull<Arena *> arena, Scene *scene)
    -> Result<RenderGraph, Error> {
  ZoneScoped;

  Renderer *renderer = scene->m_renderer;
  SwapChain *swap_chain = scene->m_swap_chain;
  SceneInternalData *sid = scene->m_sid;
  GpuScene &gpu_scene = scene->m_gpu_scene;
  FrameResources *frcs = scene->m_frcs;

  auto &pass_cfg = sid->m_pass_cfg;
  PassPersistentResources &pass_rcs = sid->m_pass_rcs;
  RgPersistent &rgp = sid->m_rgp;

  bool dirty = false;
  auto set_if_changed =
      [&]<typename T>(T &config_value,
                      const std::convertible_to<T> auto &new_value) {
        if (config_value != new_value) {
          config_value = new_value;
          dirty = true;
        }
      };

  set_if_changed(pass_cfg.async_compute, scene->m_settings.async_compute);

  set_if_changed(pass_cfg.exposure_mode, scene->m_settings.exposure_mode);

  set_if_changed(pass_cfg.viewport, swap_chain->get_size());

  set_if_changed(pass_cfg.backbuffer_usage, swap_chain->get_usage());

  set_if_changed(pass_cfg.ssao, scene->m_settings.ssao);
  set_if_changed(pass_cfg.ssao_half_res, scene->m_settings.ssao_full_res);
  set_if_changed(pass_cfg.local_tone_mapping,
                 scene->m_settings.local_tone_mapping);
  set_if_changed(pass_cfg.ltm_pyramid_size, scene->m_settings.ltm_pyramid_size);
  set_if_changed(pass_cfg.ltm_pyramid_mip, scene->m_settings.ltm_llm_mip);

  if (dirty) {
    renderer->wait_idle();
    rgp.destroy();
    scene->m_rg_arena.clear();
    rgp = RgPersistent::init(&scene->m_rg_arena, renderer);
    rgp.m_async_compute = pass_cfg.async_compute;
    pass_rcs = {};
    pass_rcs.backbuffer = rgp.create_texture("backbuffer");
    pass_rcs.sdr = pass_rcs.backbuffer;
  }

  RgBuilder rgb;
  rgb.init(arena, &rgp, renderer, &frcs->descriptor_allocator);

  PassCommonConfig cfg = {
      .rgp = &rgp,
      .rgb = &rgb,
      .allocator = &frcs->upload_allocator,
      .pipelines = &sid->m_pipelines,
      .scene = scene,
      .rcs = &pass_rcs,
      .viewport = swap_chain->get_size(),
  };

  const SceneGraphicsSettings &settings = scene->m_settings;

  RgGpuScene rg_gpu_scene = gpu_scene_update_pass(scene, cfg);

  switch (settings.exposure_mode) {
  case sh::EXPOSURE_MODE_MANUAL: {
    float exposure = sh::manual_exposure(settings.manual_exposure,
                                         settings.exposure_compensation);
    rgb.fill_buffer("exposure", &rg_gpu_scene.exposure, exposure);
  } break;
  case sh::EXPOSURE_MODE_CAMERA: {
    float exposure = sh::camera_exposure(
        settings.camera_aperture, settings.inv_camera_shutter_time,
        settings.camera_iso, settings.exposure_compensation);
    rgb.fill_buffer("exposure", &rg_gpu_scene.exposure, exposure);

  } break;
  case sh::EXPOSURE_MODE_AUTOMATIC:
    if (scene->m_frame_index == 0) {
      rgb.fill_buffer("exposure", &rg_gpu_scene.exposure, 1.0f);
    }
    break;
  default:
    break;
  }

  glm::uvec2 viewport = swap_chain->get_size();

  if (!pass_rcs.depth_buffer) {
    pass_rcs.depth_buffer = rgp.create_texture({
        .name = "depth-buffer",
        .format = DEPTH_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId depth_buffer = pass_rcs.depth_buffer;
  RgTextureId hi_z;

  setup_early_z_pass(cfg, EarlyZPassConfig{
                              .gpu_scene = &gpu_scene,
                              .rg_gpu_scene = &rg_gpu_scene,
                              .culling_phase = CullingPhase::First,
                              .depth_buffer = &depth_buffer,
                          });
  if (scene->m_settings.instance_occulusion_culling or
      scene->m_settings.meshlet_occlusion_culling) {
    setup_hi_z_pass(cfg,
                    HiZPassConfig{.depth_buffer = depth_buffer, .hi_z = &hi_z});
  }
  setup_early_z_pass(cfg, EarlyZPassConfig{
                              .gpu_scene = &gpu_scene,
                              .rg_gpu_scene = &rg_gpu_scene,
                              .culling_phase = CullingPhase::Second,
                              .depth_buffer = &depth_buffer,
                              .hi_z = hi_z,
                          });

  RgTextureId ssao_llm;
  RgTextureId ssao_depth = pass_rcs.ssao_depth;
  if (scene->m_settings.ssao) {
    glm::uvec2 ssao_hi_z_size = {std::bit_floor(viewport.x),
                                 std::bit_floor(viewport.y)};
    // Don't need the full mip chain since after a certain stage they become
    // small enough to completely fit in cache but are much less detailed.
    i32 num_ssao_hi_z_mips =
        get_mip_chain_length(min(ssao_hi_z_size.x, ssao_hi_z_size.y));
    num_ssao_hi_z_mips =
        max<i32>(num_ssao_hi_z_mips - get_mip_chain_length(32) + 1, 1);

    if (!pass_rcs.ssao_hi_z) {
      pass_rcs.ssao_hi_z = rgp.create_texture({
          .name = "ssao-hi-z",
          .format = TinyImageFormat_R16_SFLOAT,
          .width = ssao_hi_z_size.x,
          .height = ssao_hi_z_size.y,
          .num_mips = (u32)num_ssao_hi_z_mips,
      });
    }
    RgTextureId ssao_hi_z = pass_rcs.ssao_hi_z;

    const Camera &camera = scene->m_cameras[scene->m_camera];
    glm::mat4 proj = get_projection_matrix(camera, viewport);

    {
      auto pass = rgb.create_pass({.name = "ssao-hi-z"});
      auto spd_counter = rgb.create_buffer<u32>({.init = 0});
      RgSsaoHiZArgs args = {
          .spd_counter =
              pass.write_buffer("ssao-hi-z-spd-counter", &spd_counter),
          .num_mips = (u32)num_ssao_hi_z_mips,
          .dsts = pass.write_texture("ssao-hi-z", &ssao_hi_z),
          .src = pass.read_texture(
              depth_buffer,
              {
                  .mag_filter = rhi::Filter::Linear,
                  .min_filter = rhi::Filter::Linear,
                  .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                  .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                  .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
              }),
          .znear = camera.near,
      };
      pass.dispatch_grid_2d(sid->m_pipelines.ssao_hi_z, args, ssao_hi_z_size,
                            {sh::SPD_THREAD_ELEMS_X, sh::SPD_THREAD_ELEMS_Y});
    }

    glm::uvec2 ssao_size =
        scene->m_settings.ssao_full_res ? viewport : viewport / 2u;

    if (!pass_rcs.ssao) {
      pass_rcs.ssao = rgp.create_texture({
          .name = "ssao",
          .format = TinyImageFormat_R16_UNORM,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }
    if (!scene->m_settings.ssao_full_res and !pass_rcs.ssao_depth) {
      pass_rcs.ssao_depth = rgp.create_texture({
          .name = "ssao-depth",
          .format = TinyImageFormat_R16_SFLOAT,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }

    RgTextureId ssao = pass_rcs.ssao;
    ssao_depth = pass_rcs.ssao_depth;
    {
      auto pass = rgb.create_pass({.name = "ssao"});

      auto noise_lut = frcs->upload_allocator.allocate<float>(
          sh::SSAO_HILBERT_CURVE_SIZE * sh::SSAO_HILBERT_CURVE_SIZE);

      for (u32 y : range(sh::SSAO_HILBERT_CURVE_SIZE)) {
        for (u32 x : range(sh::SSAO_HILBERT_CURVE_SIZE)) {
          u32 h = sh::hilbert_from_2d(sh::SSAO_HILBERT_CURVE_SIZE, x, y);
          noise_lut.host_ptr[y * sh::SSAO_HILBERT_CURVE_SIZE + x] =
              sh::r1_seq(h);
        }
      }

      RgSsaoArgs args = {
          .noise_lut = noise_lut.device_ptr,
          .depth = pass.read_texture(depth_buffer, rhi::SAMPLER_NEAREST_CLAMP),
          .hi_z = pass.read_texture(ssao_hi_z, rhi::SAMPLER_NEAREST_CLAMP),
          .ssao = pass.write_texture("ssao", &ssao),
          .ssao_depth = ssao_depth
                            ? pass.write_texture("ssao-depth", &ssao_depth)
                            : RgTextureToken(),
          .num_samples = (u32)scene->m_settings.ssao_num_samples,
          .p00 = proj[0][0],
          .p11 = proj[1][1],
          .znear = camera.near,
          .rcp_p00 = 1.0f / proj[0][0],
          .rcp_p11 = 1.0f / proj[1][1],
          .radius = scene->m_settings.ssao_radius,
          .lod_bias = scene->m_settings.ssao_lod_bias,
      };
      pass.dispatch_grid_2d(sid->m_pipelines.ssao, args, ssao_size);
    }

    if (!pass_rcs.ssao_llm) {
      pass_rcs.ssao_llm = rgp.create_texture({
          .name = "ssao-llm",
          .format = TinyImageFormat_R16G16_SFLOAT,
          .width = ssao_size.x,
          .height = ssao_size.y,
      });
    }
    ssao_llm = pass_rcs.ssao_llm;
    {
      auto pass = rgb.create_pass({.name = "ssao-filter"});

      RgSsaoFilterArgs args = {
          .depth =
              !ssao_depth ? pass.read_texture(depth_buffer) : RgTextureToken(),
          .ssao = pass.read_texture(ssao),
          .ssao_depth = pass.try_read_texture(ssao_depth),
          .ssao_llm = pass.write_texture("ssao-llm", &ssao_llm),
          .znear = camera.near,
      };
      pass.dispatch(sid->m_pipelines.ssao_filter, args,
                    ceil_div(ssao_size.x, sh::SSAO_FILTER_GROUP_SIZE.x *
                                              sh::SSAO_FILTER_UNROLL.x),
                    ceil_div(ssao_size.y, sh::SSAO_FILTER_GROUP_SIZE.y *
                                              sh::SSAO_FILTER_UNROLL.y));
    }
  }

  if (!pass_rcs.hdr) {
    pass_rcs.hdr = rgp.create_texture({
        .name = "hdr",
        .format = HDR_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  RgTextureId hdr = pass_rcs.hdr;

  setup_opaque_pass(cfg, OpaquePassConfig{
                             .gpu_scene = &gpu_scene,
                             .rg_gpu_scene = &rg_gpu_scene,
                             .hdr = &hdr,
                             .depth_buffer = &depth_buffer,
                             .hi_z = hi_z,
                             .ssao = ssao_llm,
                         });

  setup_skybox_pass(cfg, SkyboxPassConfig{
                             .exposure = rg_gpu_scene.exposure,
                             .hdr = &hdr,
                             .depth_buffer = depth_buffer,
                         });

  if (!cfg.rcs->acquire_semaphore) {
    cfg.rcs->acquire_semaphore = rgp.create_semaphore("acquire-semaphore");
  }

  rhi::ImageUsageFlags swap_chain_usage = rhi::ImageUsage::UnorderedAccess;

  RgTextureId sdr;
  setup_post_processing_passes(cfg, PostProcessingPassesConfig{
                                        .frame_index = scene->m_frame_index,
                                        .hdr = hdr,
                                        .exposure = &rg_gpu_scene.exposure,
                                        .sdr = &sdr,
                                    });
#if REN_IMGUI
  if (ImGui::GetCurrentContext() and ImGui::GetDrawData()) {
    swap_chain_usage |= rhi::ImageUsage::RenderTarget;
    setup_imgui_pass(cfg, ImGuiPassConfig{
                              .sdr = &sdr,
                          });
  }
#endif

  swap_chain->set_usage(swap_chain_usage);

  setup_present_pass(cfg, PresentPassConfig{
                              .src = sdr,
                              .acquire_semaphore = frcs->acquire_semaphore,
                              .swap_chain = swap_chain,
                          });

  return rgb.build({
      .gfx_allocator = &sid->m_gfx_allocator,
      .async_allocator = &sid->m_async_allocator,
      .shared_allocator = &sid->m_shared_allocators[0],
      .upload_allocator = &frcs->upload_allocator,
  });
}

auto draw(Scene *scene, const DrawInfo &draw_info) -> expected<void> {
  ZoneScoped;

  ScratchArena scratch;

  scene->m_settings.middle_gray =
      glm::pow(scene->m_settings.brightness * 0.01f, 2.2f);
  scene->m_delta_time = draw_info.delta_time;

  Renderer *renderer = scene->m_renderer;
  auto *frcs = scene->m_frcs;

  ren_try_to(scene->m_sid->m_resource_uploader.upload(
      *renderer, scene->m_frcs->gfx_cmd_pool));

  ren_try(RenderGraph render_graph, build_rg(scratch, scene));

  ren_try_to(
      execute(render_graph, {
                                .gfx_cmd_pool = frcs->gfx_cmd_pool,
                                .async_cmd_pool = frcs->async_cmd_pool,
                                .frame_end_semaphore = &frcs->end_semaphore,
                                .frame_end_time = &frcs->end_time,
                            }));

  if (is_amd_anti_lag_enabled(scene)) {
    ren_try_to(renderer->amd_anti_lag_present(scene->m_frame_index));
  }

  auto present_qf = scene->m_settings.present_from_compute
                        ? rhi::QueueFamily::Compute
                        : rhi::QueueFamily::Graphics;
  ren_try_to(scene->m_swap_chain->present(present_qf));

  FrameMark;

  scene->m_frame_index++;
  next_frame(scene);

  return {};
}

auto init_imgui(NotNull<Arena *> frame_arena, Scene *scene)
    -> Result<void, Error> {
#if REN_IMGUI
  if (!ImGui::GetCurrentContext()) {
    return {};
  }

  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "imgui_impl_ren";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  u8 *data;
  i32 width, height, bpp;
  io.Fonts->GetTexDataAsRGBA32(&data, &width, &height, &bpp);
  Handle<Texture> texture = scene->m_rcs_arena
                                .create_texture({
                                    .name = "ImGui font atlas",
                                    .format = TinyImageFormat_R8G8B8A8_UNORM,
                                    .usage = rhi::ImageUsage::ShaderResource |
                                             rhi::ImageUsage::TransferDst,
                                    .width = (u32)width,
                                    .height = (u32)height,
                                })
                                .value();
  scene->m_sid->m_resource_uploader.stage_texture(
      frame_arena, scene->m_frcs->upload_allocator,
      Span((const std::byte *)data, width * height * bpp), texture);
  auto descriptor =
      scene->m_descriptor_allocator.allocate_sampled_texture<sh::Sampler2D>(
          *scene->m_renderer, SrvDesc{texture},
          {
              .mag_filter = rhi::Filter::Linear,
              .min_filter = rhi::Filter::Linear,
              .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
              .address_mode_u = rhi::SamplerAddressMode::Repeat,
              .address_mode_v = rhi::SamplerAddressMode::Repeat,
          });
  io.Fonts->SetTexID((ImTextureID)(uintptr_t)descriptor.m_id);
#endif
  return {};
}

void draw_imgui(Scene *scene) {
#if REN_IMGUI
  ZoneScoped;

  if (!ImGui::GetCurrentContext()) {
    return;
  }

  SceneGraphicsSettings &settings = scene->m_settings;

  if (ImGui::TreeNode("Async compute")) {
    ImGui::BeginDisabled(!scene->m_renderer->is_queue_family_supported(
        rhi::QueueFamily::Compute));

    ImGui::Checkbox("Async compute", &settings.async_compute);

    ImGui::BeginDisabled(!scene->m_swap_chain->is_queue_family_supported(
        rhi::QueueFamily::Compute));
    ImGui::Checkbox("Present from compute", &settings.present_from_compute);
    ImGui::EndDisabled();

    ImGui::EndDisabled();
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Latency")) {
    ImGui::BeginDisabled(!is_amd_anti_lag_available(scene));
    ImGui::Checkbox("AMD Anti-Lag", &settings.amd_anti_lag);
    ImGui::EndDisabled();

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Instance culling")) {
    ImGui::Checkbox("Frustum##InstanceCulling",
                    &settings.instance_frustum_culling);
    ImGui::Checkbox("Occlusion##InstanceCulling",
                    &settings.instance_occulusion_culling);

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Level of detail")) {
    ImGui::SliderInt("LOD bias##LOD", &settings.lod_bias,
                     -(sh::MAX_NUM_LODS - 1), sh::MAX_NUM_LODS - 1, "%d");

    ImGui::Checkbox("LOD selection##LOD", &settings.lod_selection);

    ImGui::BeginDisabled(!settings.lod_selection);
    ImGui::SliderFloat("LOD pixels per triangle##LOD",
                       &settings.lod_triangle_pixels, 1.0f, 64.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Meshlet culling")) {
    ImGui::Checkbox("Cone##MeshletCulling", &settings.meshlet_cone_culling);
    ImGui::Checkbox("Frustum##MeshletCulling",
                    &settings.meshlet_frustum_culling);
    ImGui::Checkbox("Occlusion##MeshletCulling",
                    &settings.meshlet_occlusion_culling);

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("SSAO")) {
    ImGui::Checkbox("Enabled##SSAO", &settings.ssao);

    ImGui::BeginDisabled(!settings.ssao);
    ImGui::SliderInt("Sample count##SSAO", &settings.ssao_num_samples, 1, 64,
                     "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Radius##SSAO", &settings.ssao_radius, 0.001f, 1.0f,
                       "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("LOD bias##SSAO", &settings.ssao_lod_bias, -1.0f, 4.0f);
    ImGui::Checkbox("Full resolution##SSAO", &settings.ssao_full_res);
    ImGui::EndDisabled();

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Post processing")) {
    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat(
        "Aperture", &settings.camera_aperture, 1.0f, 22.0f, "f/%.1f",
        ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Shutter time", &settings.inv_camera_shutter_time, 1.0f,
                       2000.0f, "%.0f 1/s",
                       ImGuiSliderFlags_AlwaysClamp |
                           ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("ISO", &settings.camera_iso, 100.0f, 3200.0f, "%.0f",
                       ImGuiSliderFlags_AlwaysClamp |
                           ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Exposure");

    ImGui::SliderInt("Brightness", &settings.brightness, 10, 100);

    const char *EXPOSURE_MODES[sh::EXPOSURE_MODE_COUNT] = {};
    EXPOSURE_MODES[sh::EXPOSURE_MODE_MANUAL] = "Manual";
    EXPOSURE_MODES[sh::EXPOSURE_MODE_CAMERA] = "Physical camera";
    EXPOSURE_MODES[sh::EXPOSURE_MODE_AUTOMATIC] = "Automatic";
    ImGui::ListBox("Exposure mode", (int *)&settings.exposure_mode,
                   EXPOSURE_MODES, std::size(EXPOSURE_MODES));

    ImGui::BeginDisabled(settings.exposure_mode != sh::EXPOSURE_MODE_MANUAL);
    ImGui::InputFloat("Manual exposure", &settings.manual_exposure, 1.0f, 10.0f,
                      "%.1f EV");
    ImGui::EndDisabled();

    ImGui::InputFloat("Exposure compensation", &settings.exposure_compensation,
                      1.0f, 10.0f, "%.1f EV");

    ImGui::BeginDisabled(settings.exposure_mode != sh::EXPOSURE_MODE_AUTOMATIC);

    ImGui::Checkbox("Temporal adaptation", &settings.temporal_adaptation);
    ImGui::BeginDisabled(!settings.temporal_adaptation);
    ImGui::SliderFloat("Dark adaptation time", &settings.dark_adaptation_time,
                       0.2f, 30.0f * 60.0f, nullptr,
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Bright adaptation time",
                       &settings.bright_adaptation_time, 0.05f, 5.0f * 60.0f,
                       nullptr, ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();

    const char *METERING_MODES[sh::METERING_MODE_COUNT] = {};
    METERING_MODES[sh::METERING_MODE_SPOT] = "Spot";
    METERING_MODES[sh::METERING_MODE_CENTER_WEIGHTED] = "Center-weighted";
    METERING_MODES[sh::METERING_MODE_AVERAGE] = "Average";
    ImGui::ListBox("Metering mode", (int *)&settings.metering_mode,
                   METERING_MODES, std::size(METERING_MODES));

    ImGui::BeginDisabled(settings.metering_mode != sh::METERING_MODE_SPOT);
    ImGui::SliderFloat("Spot metering pattern size",
                       &settings.spot_metering_pattern_relative_diameter, 0.1f,
                       1.0f);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(settings.metering_mode !=
                         sh::METERING_MODE_CENTER_WEIGHTED);
    ImGui::SliderFloat(
        "Center-weighted metering pattern inner size",
        &settings.center_weighted_metering_pattern_relative_inner_diameter,
        0.1f, 1.0f);
    ImGui::SliderFloat(
        "Center-weighted metering pattern outer to inner size ratio",
        &settings.center_weighted_metering_pattern_size_ratio, 1.1f, 2.0f);
    ImGui::EndDisabled();

    ImGui::EndDisabled();

    ImGui::SeparatorText("Tone mapping");
    const char *TONE_MAPPERS[sh::TONE_MAPPER_COUNT] = {};
    TONE_MAPPERS[sh::TONE_MAPPER_LINEAR] = "Linear";
    TONE_MAPPERS[sh::TONE_MAPPER_REINHARD] = "Reinhard";
    TONE_MAPPERS[sh::TONE_MAPPER_LUMINANCE_REINHARD] = "Reinhard (Luminance)";
    TONE_MAPPERS[sh::TONE_MAPPER_ACES] = "ACES";
    TONE_MAPPERS[sh::TONE_MAPPER_KHR_PBR_NEUTRAL] = "Khronos PBR Neutral";
    TONE_MAPPERS[sh::TONE_MAPPER_AGX_DEFAULT] = "AgX Default";
    TONE_MAPPERS[sh::TONE_MAPPER_AGX_PUNCHY] = "AgX Punchy";
    ImGui::ListBox("Tone mapper", (int *)&settings.tone_mapper, TONE_MAPPERS,
                   std::size(TONE_MAPPERS), std::size(TONE_MAPPERS));

    ImGui::Checkbox("Local tone mapping", &settings.local_tone_mapping);
    ImGui::BeginDisabled(!settings.local_tone_mapping);
    ImGui::SliderFloat("Local tone mapping shadows", &settings.ltm_shadows,
                       0.0f, 4.0f);
    ImGui::SliderFloat("Local tone mapping highlights",
                       &settings.ltm_highlights, 0.0f, 4.0f);
    ImGui::SliderFloat("Local tone mapping sigma", &settings.ltm_sigma, 0.0f,
                       5.0f);
    ImGui::Checkbox("Local tone mapping contrast boost",
                    &settings.ltm_contrast_boost);
    ImGui::SliderInt("Local tone mapping pyramid size",
                     &settings.ltm_pyramid_size, 1, 10);
    ImGui::SliderInt("Local tone mapping LLM mip", &settings.ltm_llm_mip, 0, 9);
    ImGui::EndDisabled();

    ImGui::Checkbox("Dithering", &settings.dithering);

    ImGui::TreePop();
  }
#endif
}

} // namespace ren_export

namespace ren::hot_reload {

void unload(Scene *scene) {
  scene->m_renderer->wait_idle();
  destroy_internal_data(scene);
  scene->m_internal_arena.clear();
  scene->m_rg_arena.clear();
  scene->m_sid = nullptr;
  unload(scene->m_renderer);
}

auto load(Scene *scene) -> expected<void> {
  ren_try_to(load(scene->m_renderer));
  ren_try_to(init_internal_data(scene));
  next_frame(scene);
  return {};
}

} // namespace ren::hot_reload
