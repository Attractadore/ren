#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Support/Views.hpp"
#include "glsl/Batch.h"
#include "glsl/EarlyZPass.h"
#include "glsl/InstanceCullingAndLODPass.h"
#include "glsl/OpaquePass.h"

#include <range/v3/view.hpp>

namespace ren {

namespace {

struct BatchData {
  Vector<u32> counts;
  Vector<u32> offsets;
};

struct UploadPassResources {
  RgRWVariableToken<BatchData> batches;

  RgBufferToken meshes;
  RgBufferToken materials;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken normal_matrices;
  RgBufferToken directional_lights;

  RgBufferToken batch_offsets;
};

void run_upload_pass(Renderer &renderer, const RgRuntime &rg,
                     const Scene &scene, const UploadPassResources &rcs) {
  assert(rcs.materials);
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);

  const Camera &camera = scene.get_camera();
  glm::uvec2 viewport = scene.get_viewport();

  glm::mat4 proj = get_projection_matrix(camera, viewport);
  glm::mat4 view = get_view_matrix(camera);

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  ranges::copy(scene.get_materials(), materials);

  auto *meshes_ptr = rg.map_buffer<glsl::Mesh>(rcs.meshes);
  Span<const Mesh> meshes = scene.get_meshes();

  for (auto i : range<usize>(1, meshes.size())) {
    const Mesh &mesh = meshes[i];
    meshes_ptr[i] = {
        .positions =
            renderer.get_buffer_device_ptr<glsl::Position>(mesh.positions),
        .normals = renderer.get_buffer_device_ptr<glsl::Normal>(mesh.normals),
        .tangents =
            renderer.try_get_buffer_device_ptr<glsl::Tangent>(mesh.tangents),
        .uvs = renderer.try_get_buffer_device_ptr<glsl::UV>(mesh.uvs),
        .colors = renderer.try_get_buffer_device_ptr<glsl::Color>(mesh.colors),
        .bb = mesh.bb,
        .uv_bs = mesh.uv_bs,
        .index_pool = mesh.index_pool,
        .num_lods = u32(mesh.lods.size()),
    };
    ranges::copy(mesh.lods, meshes_ptr[i].lods);
  }

  auto *mesh_instances_ptr =
      rg.map_buffer<glsl::MeshInstance>(rcs.mesh_instances);
  auto *transform_matrices_ptr =
      rg.map_buffer<glm::mat4x3>(rcs.transform_matrices);
  auto *normal_matrices_ptr = rg.map_buffer<glm::mat3>(rcs.normal_matrices);

  Span<const MeshInstance> mesh_instances = scene.get_mesh_instances();

  for (const auto &[i, mesh_instance] : mesh_instances | enumerate) {
    mesh_instances_ptr[i] = {
        .mesh = mesh_instance.mesh,
        .material = mesh_instance.material,
    };
    transform_matrices_ptr[i] = mesh_instance.matrix;
    normal_matrices_ptr[i] =
        glm::transpose(glm::inverse(glm::mat3(mesh_instance.matrix)));
  }

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  ranges::copy(scene.get_directional_lights(), directional_lights);

  BatchData &batches = rg.get_variable(rcs.batches);
  {
    batches.counts.resize(glsl::MAX_NUM_BATCHES);
    ranges::fill(batches.counts, 0);
    for (const MeshInstance &mesh_instance : mesh_instances) {
      const Mesh &mesh = meshes[mesh_instance.mesh];
      u32 batch_id =
          glsl::make_batch(get_mesh_attribute_mask(mesh), mesh.index_pool).id;
      batches.counts[batch_id]++;
    }
    batches.offsets.assign(batches.counts | ranges::views::exclusive_scan(0));
  }
  auto *batch_offsets = rg.map_buffer<u32>(rcs.batch_offsets);
  ranges::copy(batches.offsets, batch_offsets);
}

struct UploadPassConfig {
  u32 num_meshes;
  u32 num_mesh_instances;
  u32 num_materials;
  u32 num_directional_lights;
};

struct UploadPassOutput {
  RgVariableId<BatchData> batches;

  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;

  RgBufferId batch_offsets;
};

auto setup_upload_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                       const UploadPassConfig &cfg) -> UploadPassOutput {
  UploadPassResources rcs;
  UploadPassOutput out;

  auto pass = rgb.create_pass({.name = "upload"});

  std::tie(out.batches, rcs.batches) =
      pass.create_variable<BatchData>("batch-data");

  std::tie(out.materials, rcs.materials) = pass.create_buffer(
      {
          .name = "materials",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::Material) * cfg.num_materials,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.meshes, rcs.meshes) = pass.create_buffer(
      {
          .name = "meshes",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::Mesh) * cfg.num_meshes,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.mesh_instances, rcs.mesh_instances) = pass.create_buffer(
      {
          .name = "mesh-instances",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::MeshInstance) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.batch_offsets, rcs.batch_offsets) = pass.create_buffer(
      {
          .name = "batch-offsets",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(u32[glsl::MAX_NUM_BATCHES]),
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.transform_matrices, rcs.transform_matrices) = pass.create_buffer(
      {
          .name = "transform-matrices",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::mat4x3) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.normal_matrices, rcs.normal_matrices) = pass.create_buffer(
      {
          .name = "normal-matrices",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glm::mat3) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  std::tie(out.directional_lights, rcs.directional_lights) = pass.create_buffer(
      {
          .name = "directional-lights",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::DirLight) * cfg.num_directional_lights,
      },
      RG_HOST_WRITE_BUFFER);

  pass.set_host_callback([=](Renderer &renderer, const RgRuntime &rt) {
    run_upload_pass(renderer, rt, *scene, rcs);
  });

  return out;
}

struct InstanceCullingAndLODPassResources {
  RgBufferToken meshes;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken commands;
  RgBufferToken batch_offsets;
  RgBufferToken batch_counts;
};

void run_instance_culling_and_lod_pass(
    const RgRuntime &rg, const Scene &scene, ComputePass &pass,
    const InstanceCullingAndLODPassResources &rcs) {
  u32 feature_mask = 0;
  if (scene.is_frustum_culling_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
  }
  if (scene.is_lod_selection_enabled()) {
    feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
  }

  glm::uvec2 viewport = scene.get_viewport();

  float num_viewport_triangles =
      viewport.x * viewport.y / scene.get_lod_triangle_pixel_count();
  float lod_triangle_density = num_viewport_triangles / 4.0f;

  u32 num_mesh_instances = scene.get_mesh_instances().size();

  pass.bind_compute_pipeline(scene.get_pipelines().instance_culling_and_lod);
  pass.set_push_constants(glsl::InstanceCullingAndLODPassArgs{
      .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
      .mesh_instances =
          rg.get_buffer_device_ptr<glsl::MeshInstance>(rcs.mesh_instances),
      .transform_matrices =
          rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
      .batch_command_offsets =
          rg.get_buffer_device_ptr<glm::uint>(rcs.batch_offsets),
      .batch_command_counts =
          rg.get_buffer_device_ptr<glm::uint>(rcs.batch_counts),
      .commands = rg.get_buffer_device_ptr<glsl::DrawIndexedIndirectCommand>(
          rcs.commands),
      .feature_mask = feature_mask,
      .num_mesh_instances = num_mesh_instances,
      .proj_view = scene.get_camera_proj_view(),
      .lod_triangle_density = lod_triangle_density,
      .lod_bias = scene.get_lod_bias(),
  });
  pass.dispatch_threads(num_mesh_instances,
                        glsl::INSTANCE_CULLING_AND_LOD_THREADS);
}

struct InstanceCullingAndLODPassConfig {
  u32 num_mesh_instances;
  RgBufferId meshes;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId batch_offsets;
};

struct InstanceCullingAndLODPassOutput {
  RgBufferId commands;
  RgBufferId batch_counts;
};

auto setup_instance_culling_and_lod_pass(
    RgBuilder &rgb, NotNull<const Scene *> scene,
    const InstanceCullingAndLODPassConfig &cfg)
    -> InstanceCullingAndLODPassOutput {
  InstanceCullingAndLODPassResources rcs;
  InstanceCullingAndLODPassOutput out;

  {
    auto pass = rgb.create_pass({.name = "init-instance-culling-and-lod"});

    RgBufferToken batch_counts;
    std::tie(out.batch_counts, batch_counts) = pass.create_buffer(
        {
            .name = "batch-counts-zero",
            .heap = BufferHeap::Static,
            .size = sizeof(u32[glsl::MAX_NUM_BATCHES]),
        },
        RG_TRANSFER_DST_BUFFER);

    pass.set_callback(
        [=](Renderer &renderer, const RgRuntime &rt, CommandRecorder &cmd) {
          cmd.fill_buffer(rt.get_buffer(batch_counts), 0);
        });
  }

  auto pass = rgb.create_pass({.name = "instance-culling-and-lod"});

  rcs.meshes = pass.read_buffer(cfg.meshes, RG_CS_READ_BUFFER);

  rcs.mesh_instances = pass.read_buffer(cfg.mesh_instances, RG_CS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(cfg.transform_matrices, RG_CS_READ_BUFFER);

  std::tie(out.commands, rcs.commands) = pass.create_buffer(
      {
          .name = "indirect-commands",
          .heap = BufferHeap::Static,
          .size =
              sizeof(glsl::DrawIndexedIndirectCommand) * cfg.num_mesh_instances,
      },
      RG_CS_WRITE_BUFFER);

  rcs.batch_offsets = pass.read_buffer(cfg.batch_offsets, RG_CS_READ_BUFFER);

  std::tie(out.batch_counts, rcs.batch_counts) = pass.write_buffer(
      "batch-counts", out.batch_counts, RG_CS_READ_BUFFER | RG_CS_WRITE_BUFFER);

  pass.set_compute_callback(
      [=](Renderer &, const RgRuntime &rg, ComputePass &pass) {
        run_instance_culling_and_lod_pass(rg, *scene, pass, rcs);
      });

  return out;
};

struct EarlyZPassResources {
  RgVariableToken<BatchData> batches;
  RgBufferToken commands;
  RgBufferToken batch_counts;
  RgBufferToken meshes;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
};

void run_early_z_pass(const RgRuntime &rg, const Scene &scene,
                      RenderPass &render_pass, const EarlyZPassResources &rcs) {
  const BatchData &batches = rg.get_variable(rcs.batches);

  if (scene.get_mesh_instances().empty()) {
    return;
  }

  const BufferView &commands = rg.get_buffer(rcs.commands);
  const BufferView &batch_counts = rg.get_buffer(rcs.batch_counts);

  render_pass.bind_graphics_pipeline(scene.get_pipelines().early_z_pass);
  render_pass.set_push_constants(glsl::EarlyZPassArgs{
      .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
      .mesh_instances =
          rg.get_buffer_device_ptr<glsl::MeshInstance>(rcs.mesh_instances),
      .transform_matrices =
          rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
      .proj_view = scene.get_camera_proj_view(),
  });
  for (u32 attribute_mask = 0; attribute_mask < glsl::NUM_MESH_ATTRIBUTE_FLAGS;
       ++attribute_mask) {
    for (const auto &[i, pool] : scene.get_index_pools() | enumerate) {
      render_pass.bind_index_buffer(pool.indices, VK_INDEX_TYPE_UINT32);
      u32 batch_id = glsl::make_batch(attribute_mask, i).id;
      u32 offset = batches.offsets[batch_id];
      u32 count = batches.counts[batch_id];
      render_pass.draw_indexed_indirect_count(
          commands.slice<glsl::DrawIndexedIndirectCommand>(offset, count),
          batch_counts.slice<u32>(batch_id));
    }
  }
}

struct EarlyZPassConfig {
  RgVariableId<BatchData> batch;
  RgBufferId meshes;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId commands;
  RgBufferId batch_counts;
};

auto setup_early_z_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                        const EarlyZPassConfig &cfg) -> RgTextureId {
  EarlyZPassResources rcs;

  auto pass = rgb.create_pass({.name = "early-z"});

  rcs.batches = pass.read_variable(cfg.batch);

  rcs.commands = pass.read_buffer(cfg.commands, RG_INDIRECT_COMMAND_BUFFER);

  rcs.batch_counts =
      pass.read_buffer(cfg.batch_counts, RG_INDIRECT_COMMAND_BUFFER);

  rcs.meshes = pass.read_buffer(cfg.meshes, RG_VS_READ_BUFFER);

  rcs.mesh_instances = pass.read_buffer(cfg.mesh_instances, RG_VS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(cfg.transform_matrices, RG_VS_READ_BUFFER);

  glm::uvec2 viewport = scene->get_viewport();

  auto [depth_buffer, _] = pass.create_depth_attachment(
      {
          .name = "depth-buffer",
          .format = DEPTH_FORMAT,
          .width = viewport.x,
          .height = viewport.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_graphics_callback(
      [=](Renderer &, const RgRuntime &rt, RenderPass &render_pass) {
        run_early_z_pass(rt, *scene, render_pass, rcs);
      });

  return depth_buffer;
}

struct OpaquePassResources {
  RgVariableToken<BatchData> batches;
  RgBufferToken commands;
  RgBufferToken batch_counts;
  RgBufferToken uniforms;
  RgBufferToken meshes;
  RgBufferToken materials;
  RgBufferToken mesh_instances;
  RgBufferToken transform_matrices;
  RgBufferToken normal_matrices;
  RgBufferToken directional_lights;
  RgTextureToken exposure;
};

void run_opaque_pass(const RgRuntime &rg, const Scene &scene,
                     RenderPass &render_pass, const OpaquePassResources &rcs) {
  const BatchData &batches = rg.get_variable(rcs.batches);

  if (scene.get_mesh_instances().empty()) {
    return;
  }

  const BufferView &commands = rg.get_buffer(rcs.commands);
  const BufferView &batch_counts = rg.get_buffer(rcs.batch_counts);

  auto *uniforms = rg.map_buffer<glsl::OpaquePassUniforms>(rcs.uniforms);
  *uniforms = glsl::OpaquePassUniforms{
      .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
      .mesh_instances =
          rg.get_buffer_device_ptr<glsl::MeshInstance>(rcs.mesh_instances),
      .transform_matrices =
          rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
      .normal_matrices =
          rg.get_buffer_device_ptr<glm::mat3>(rcs.normal_matrices),
      .proj_view = scene.get_camera_proj_view(),
  };

  auto ub = rg.get_buffer_device_ptr<glsl::OpaquePassUniforms>(rcs.uniforms);

  for (u32 attribute_mask = 0; attribute_mask < glsl::NUM_MESH_ATTRIBUTE_FLAGS;
       ++attribute_mask) {
    render_pass.bind_graphics_pipeline(
        scene.get_pipelines().opaque_pass[attribute_mask]);
    render_pass.bind_descriptor_sets({rg.get_texture_set()});
    render_pass.set_push_constants(glsl::OpaquePassArgs{
        .ub = ub,
        .materials = rg.get_buffer_device_ptr<glsl::Material>(rcs.materials),
        .directional_lights =
            rg.get_buffer_device_ptr<glsl::DirLight>(rcs.directional_lights),
        .num_directional_lights = u32(scene.get_directional_lights().size()),
        .eye = scene.get_camera().position,
        .exposure_texture = rg.get_storage_texture_descriptor(rcs.exposure),
    });
    for (const auto &[i, pool] : scene.get_index_pools() | enumerate) {
      render_pass.bind_index_buffer(pool.indices, VK_INDEX_TYPE_UINT32);
      u32 batch_id = glsl::make_batch(attribute_mask, i).id;
      u32 offset = batches.offsets[batch_id];
      u32 count = batches.counts[batch_id];
      render_pass.draw_indexed_indirect_count(
          commands.slice<glsl::DrawIndexedIndirectCommand>(offset, count),
          batch_counts.slice<u32>(batch_id));
    }
  }
}

struct OpaquePassConfig {
  ExposurePassOutput exposure;
  RgVariableId<BatchData> batches;
  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  RgBufferId commands;
  RgBufferId batch_counts;
  RgTextureId depth_buffer;
};

auto setup_opaque_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                       const OpaquePassConfig &cfg) -> RgTextureId {
  OpaquePassResources rcs;
  auto pass = rgb.create_pass({.name = "opaque"});

  rcs.batches = pass.read_variable(cfg.batches);

  rcs.commands = pass.read_buffer(cfg.commands, RG_INDIRECT_COMMAND_BUFFER);

  rcs.batch_counts =
      pass.read_buffer(cfg.batch_counts, RG_INDIRECT_COMMAND_BUFFER);

  std::tie(std::ignore, rcs.uniforms) = pass.create_buffer(
      {
          .name = "opaque-pass-uniforms",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::OpaquePassUniforms),
      },
      RG_HOST_WRITE_BUFFER | RG_VS_READ_BUFFER | RG_FS_READ_BUFFER);

  rcs.meshes = pass.read_buffer(cfg.meshes, RG_VS_READ_BUFFER);

  rcs.materials = pass.read_buffer(cfg.materials, RG_FS_READ_BUFFER);

  rcs.mesh_instances = pass.read_buffer(cfg.mesh_instances, RG_VS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(cfg.transform_matrices, RG_VS_READ_BUFFER);

  rcs.normal_matrices =
      pass.read_buffer(cfg.normal_matrices, RG_VS_READ_BUFFER);

  rcs.directional_lights =
      pass.read_buffer(cfg.directional_lights, RG_FS_READ_BUFFER);

  rcs.exposure = pass.read_texture(cfg.exposure.exposure, RG_FS_READ_TEXTURE,
                                   cfg.exposure.temporal_layer);

  glm::uvec2 viewport = scene->get_viewport();

  auto [rt, _] = pass.create_color_attachment(
      {
          .name = "hdr",
          .format = HDR_FORMAT,
          .width = viewport.x,
          .height = viewport.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
      });

  if (scene->is_early_z_enabled()) {
    (void)pass.read_depth_attachment(cfg.depth_buffer);
  } else {
    (void)pass.create_depth_attachment(
        {
            .name = "depth-buffer",
            .format = DEPTH_FORMAT,
            .width = viewport.x,
            .height = viewport.y,
        },
        {
            .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .store = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clear_depth = 0.0f,
        });
  }

  pass.set_graphics_callback(
      [=](Renderer &, const RgRuntime &rt, RenderPass &render_pass) {
        run_opaque_pass(rt, *scene, render_pass, rcs);
      });

  return rt;
}

} // namespace

auto setup_opaque_passes(RgBuilder &rgb, NotNull<const Scene *> scene,
                         const OpaquePassesConfig &cfg) -> RgTextureId {
  UploadPassOutput upload = setup_upload_pass(
      rgb, scene,
      UploadPassConfig{
          .num_meshes = cfg.num_meshes,
          .num_mesh_instances = cfg.num_mesh_instances,
          .num_materials = cfg.num_materials,
          .num_directional_lights = cfg.num_directional_lights,
      });

  InstanceCullingAndLODPassOutput instance_culling_and_lod =
      setup_instance_culling_and_lod_pass(
          rgb, scene,
          InstanceCullingAndLODPassConfig{
              .num_mesh_instances = cfg.num_mesh_instances,
              .meshes = upload.meshes,
              .mesh_instances = upload.mesh_instances,
              .transform_matrices = upload.transform_matrices,
              .batch_offsets = upload.batch_offsets,
          });

  RgTextureId depth_buffer;
  if (scene->is_early_z_enabled()) {
    depth_buffer = setup_early_z_pass(
        rgb, scene,
        EarlyZPassConfig{
            .batch = upload.batches,
            .meshes = upload.meshes,
            .mesh_instances = upload.mesh_instances,
            .transform_matrices = upload.transform_matrices,
            .commands = instance_culling_and_lod.commands,
            .batch_counts = instance_culling_and_lod.batch_counts,
        });
  }

  return setup_opaque_pass(
      rgb, scene,
      OpaquePassConfig{
          .exposure = cfg.exposure,
          .batches = upload.batches,
          .meshes = upload.meshes,
          .materials = upload.materials,
          .mesh_instances = upload.mesh_instances,
          .transform_matrices = upload.transform_matrices,
          .normal_matrices = upload.normal_matrices,
          .directional_lights = upload.directional_lights,
          .commands = instance_culling_and_lod.commands,
          .batch_counts = instance_culling_and_lod.batch_counts,
          .depth_buffer = depth_buffer,
      });
}

} // namespace ren
