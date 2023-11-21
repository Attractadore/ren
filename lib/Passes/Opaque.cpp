#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/Batch.hpp"
#include "glsl/EarlyZPass.hpp"
#include "glsl/InstanceCulling.hpp"
#include "glsl/OpaquePass.hpp"

#include <range/v3/view.hpp>

namespace ren {

namespace {

const char UPLOAD_PASS[] = "upload";
const char EARLY_Z_PASS[] = "early-z";
const char OPAQUE_PASS[] = "opaque";
const char INSTANCE_CULLING_INIT_PASS[] = "init-instance-culling";
const char INSTANCE_CULLING_PASS[] = "instance-culling";

const char MESH_CULL_DATA_BUFFER[] = "mesh-cull-data";
const char MESH_INSTANCE_CULL_DATA_BUFFER[] = "mesh-instance-cull-data";
const char BATCH_COMMAND_OFFSETS_BUFFER[] = "batch-command-offsets";

const char INDIRECT_COMMANDS_BUFFER[] = "indirect-commands";
const char BATCH_COMMAND_COUNTS_BUFFER[] = "batch-command-counts";

const char MATERIALS_BUFFER[] = "materials";
const char MESH_INSTANCE_DRAW_DATA_BUFFER[] = "mesh-instance-draw-data";
const char TRANSFORM_MATRICES_BUFFER[] = "transform-matrices";
const char NORMAL_MATRICES_BUFFER[] = "normal-matrices";
const char DIRECTIONAL_LIGHTS_BUFFER[] = "directional-lights";

const char DEPTH_BUFFER[] = "depth-buffer";

struct UploadPassResources {
  RgBufferId materials;
  RgBufferId mesh_instance_draw_data;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;

  RgBufferId mesh_cull_data;
  RgBufferId mesh_instance_cull_data;
  RgBufferId batch_command_offsets;
};

struct UploadPassData {
  Span<const u32> batch_offsets;
  Span<const Mesh> meshes;
  Span<const glsl::Material> materials;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::DirLight> directional_lights;
};

void run_upload_pass(const RgRuntime &rg, const UploadPassResources &rcs,
                     const UploadPassData &data) {
  assert(rcs.materials);
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  ranges::copy(data.materials, materials);

  auto *mesh_cull_data = rg.map_buffer<glsl::MeshCullData>(rcs.mesh_cull_data);
  for (const auto &[i, mesh] : data.meshes | enumerate) {
    auto attribute_mask = static_cast<uint8_t>(mesh.attributes.get());
    uint8_t pool = mesh.pool;
    mesh_cull_data[i] = {
        .attribute_mask = attribute_mask,
        .pool = pool,
        .bb = mesh.bounding_box,
        .base_vertex = mesh.base_vertex,
        .num_lods = u32(mesh.lods.size()),
    };
    ranges::copy(mesh.lods, mesh_cull_data[i].lods);
  }

  auto *mesh_instance_cull_data =
      rg.map_buffer<glsl::MeshInstanceCullData>(rcs.mesh_instance_cull_data);
  auto *mesh_instance_draw_data =
      rg.map_buffer<glsl::MeshInstanceDrawData>(rcs.mesh_instance_draw_data);
  auto *transform_matrices = rg.map_buffer<glm::mat4x3>(rcs.transform_matrices);
  auto *normal_matrices = rg.map_buffer<glm::mat3>(rcs.normal_matrices);
  for (const auto &[i, mesh_instance] : data.mesh_instances | enumerate) {
    const Mesh &mesh = data.meshes[mesh_instance.mesh];
    mesh_instance_cull_data[i] = {
        .mesh = mesh_instance.mesh,
    };
    mesh_instance_draw_data[i] = {
        .uv_bs = mesh.uv_bounding_square,
        .material = mesh_instance.material,
    };
    transform_matrices[i] = mesh_instance.matrix;
    normal_matrices[i] =
        glm::transpose(glm::inverse(glm::mat3(mesh_instance.matrix)));
  }

  auto *batch_offsets = rg.map_buffer<u32>(rcs.batch_command_offsets);
  ranges::copy(data.batch_offsets, batch_offsets);

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  ranges::copy(data.directional_lights, directional_lights);
}

void setup_upload_pass(RgBuilder &rgb) {
  auto pass = rgb.create_pass(UPLOAD_PASS);

  UploadPassResources rcs;

  rcs.materials =
      pass.create_buffer({.name = MATERIALS_BUFFER}, RG_HOST_WRITE_BUFFER);

  rcs.mesh_cull_data = pass.create_buffer(
      {
          .name = MESH_CULL_DATA_BUFFER,
          .heap = BufferHeap::Dynamic,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.mesh_instance_cull_data = pass.create_buffer(
      {
          .name = MESH_INSTANCE_CULL_DATA_BUFFER,
          .heap = BufferHeap::Dynamic,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.batch_command_offsets = pass.create_buffer(
      {
          .name = BATCH_COMMAND_OFFSETS_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(u32[glsl::NUM_BATCHES]),
      },
      RG_HOST_WRITE_BUFFER);

  rcs.mesh_instance_draw_data = pass.create_buffer(
      {.name = MESH_INSTANCE_DRAW_DATA_BUFFER}, RG_HOST_WRITE_BUFFER);

  rcs.transform_matrices = pass.create_buffer(
      {.name = TRANSFORM_MATRICES_BUFFER}, RG_HOST_WRITE_BUFFER);

  rcs.normal_matrices = pass.create_buffer({.name = NORMAL_MATRICES_BUFFER},
                                           RG_HOST_WRITE_BUFFER);

  rcs.directional_lights = pass.create_buffer(
      {.name = DIRECTIONAL_LIGHTS_BUFFER}, RG_HOST_WRITE_BUFFER);

  pass.set_update_callback(ren_rg_update_callback(UploadPassData) {
    rg.resize_buffer(rcs.materials,
                     sizeof(glsl::Material) * data.materials.size());
    rg.resize_buffer(rcs.mesh_cull_data,
                     sizeof(glsl::MeshCullData) * data.meshes.size());
    rg.resize_buffer(rcs.mesh_instance_cull_data,
                     sizeof(glsl::MeshInstanceCullData) *
                         data.mesh_instances.size());
    rg.resize_buffer(rcs.mesh_instance_draw_data,
                     sizeof(glsl::MeshInstanceDrawData) *
                         data.mesh_instances.size());
    rg.resize_buffer(rcs.transform_matrices,
                     sizeof(glm::mat4x3) * data.mesh_instances.size());
    rg.resize_buffer(rcs.normal_matrices,
                     sizeof(glm::mat3) * data.mesh_instances.size());
    rg.resize_buffer(rcs.directional_lights,
                     data.directional_lights.size_bytes());
    return true;
  });

  pass.set_host_callback(
      ren_rg_host_callback(UploadPassData) { run_upload_pass(rg, rcs, data); });
}

struct InstanceCullingPassResources {
  Handle<ComputePipeline> pipeline;
  RgBufferId meshes;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId commands;
  RgBufferId batch_offsets;
  RgBufferId batch_counts;
};

struct InstanceCullingPassData {
  u32 num_mesh_instances = 0;
  glm::mat4 pv;
  bool frustum_culling : 1 = true;
};

void run_instance_culling_pass(const RgRuntime &rg, ComputePass &pass,
                               const InstanceCullingPassResources &rcs,
                               const InstanceCullingPassData &data) {
  u32 mask = 0;
  if (data.frustum_culling) {
    mask |= glsl::INSTANCE_CULLING_FRUSTUM_BIT;
  }
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.set_push_constants(glsl::InstanceCullingConstants{
      .meshes = g_renderer->get_buffer_device_address<glsl::CullMeshes>(
          rg.get_buffer(rcs.meshes)),
      .mesh_instances =
          g_renderer->get_buffer_device_address<glsl::CullMeshInstances>(
              rg.get_buffer(rcs.mesh_instances)),
      .transform_matrices =
          g_renderer->get_buffer_device_address<glsl::TransformMatrices>(
              rg.get_buffer(rcs.transform_matrices)),
      .batch_command_offsets =
          g_renderer->get_buffer_device_address<glsl::BatchCommandOffsets>(
              rg.get_buffer(rcs.batch_offsets)),
      .batch_command_counts =
          g_renderer->get_buffer_device_address<glsl::BatchCommandCounts>(
              rg.get_buffer(rcs.batch_counts)),
      .commands = g_renderer->get_buffer_device_address<
          glsl::DrawIndexedIndirectCommands>(rg.get_buffer(rcs.commands)),
      .mask = mask,
      .num_mesh_instances = data.num_mesh_instances,
      .pv = data.pv,
  });
  pass.dispatch_threads(data.num_mesh_instances,
                        glsl::INSTANCE_CULLING_THREADS);
}

struct InstanceCullingPassConfig {
  Handle<ComputePipeline> pipeline;
};

void setup_instance_culling_pass(RgBuilder &rgb,
                                 const InstanceCullingPassConfig &cfg) {
  InstanceCullingPassResources rcs;
  rcs.pipeline = cfg.pipeline;

  String batch_counts_empty =
      fmt::format("{}-empty", BATCH_COMMAND_COUNTS_BUFFER);
  {
    auto init_pass = rgb.create_pass(INSTANCE_CULLING_INIT_PASS);

    RgBufferId batch_counts = init_pass.create_buffer(
        {
            .name = batch_counts_empty,
            .heap = BufferHeap::Static,
            .size = sizeof(u32[glsl::NUM_BATCHES]),
        },
        RG_TRANSFER_DST_BUFFER);

    init_pass.set_transfer_callback(ren_rg_transfer_callback(RgNoPassData) {
      cmd.fill_buffer(rg.get_buffer(batch_counts), 0);
    });
  }

  auto pass = rgb.create_pass(INSTANCE_CULLING_PASS);

  rcs.meshes = pass.read_buffer(MESH_CULL_DATA_BUFFER, RG_CS_READ_BUFFER);

  rcs.mesh_instances =
      pass.read_buffer(MESH_INSTANCE_CULL_DATA_BUFFER, RG_CS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(TRANSFORM_MATRICES_BUFFER, RG_CS_READ_BUFFER);

  rcs.commands = pass.create_buffer(
      {
          .name = INDIRECT_COMMANDS_BUFFER,
          .heap = BufferHeap::Static,
      },
      RG_CS_WRITE_BUFFER);

  rcs.batch_offsets =
      pass.read_buffer(BATCH_COMMAND_OFFSETS_BUFFER, RG_CS_READ_BUFFER);

  rcs.batch_counts =
      pass.write_buffer(BATCH_COMMAND_COUNTS_BUFFER, batch_counts_empty,
                        RG_CS_READ_BUFFER | RG_CS_WRITE_BUFFER);

  pass.set_update_callback(ren_rg_update_callback(InstanceCullingPassData) {
    rg.resize_buffer(rcs.commands, sizeof(glsl::DrawIndexedIndirectCommand) *
                                       data.num_mesh_instances);
    return true;
  });

  pass.set_compute_callback(ren_rg_compute_callback(InstanceCullingPassData) {
    run_instance_culling_pass(rg, pass, rcs, data);
  });
};

struct EarlyZPassResources {
  Handle<GraphicsPipeline> pipeline;
  RgBufferId commands;
  RgBufferId batch_counts;
  RgBufferId transform_matrices;
};

struct EarlyZPassData {
  Span<const u32> batch_offsets;
  Span<const u32> batch_max_counts;
  Span<const VertexPoolList> vertex_pool_lists;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  glm::uvec2 viewport;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
};

void run_early_z_pass(const RgRuntime &rg, RenderPass &render_pass,
                      const EarlyZPassResources &rcs,
                      const EarlyZPassData &data) {
  if (data.mesh_instances.empty()) {
    return;
  }

  const BufferView &commands = rg.get_buffer(rcs.commands);
  const BufferView &batch_counts = rg.get_buffer(rcs.batch_counts);
  auto transform_matrices =
      g_renderer->get_buffer_device_address<glsl::TransformMatrices>(
          rg.get_buffer(rcs.transform_matrices));
  glm::mat4 proj_view = data.proj * data.view;

  render_pass.bind_graphics_pipeline(rcs.pipeline);
  for (u32 attribute_mask = 0; attribute_mask < glsl::NUM_MESH_ATTRIBUTE_FLAGS;
       ++attribute_mask) {
    for (const auto &[pool, vertex_pool] :
         data.vertex_pool_lists[attribute_mask] | enumerate) {
      render_pass.bind_index_buffer(vertex_pool.indices, VK_INDEX_TYPE_UINT32);
      render_pass.set_push_constants(glsl::EarlyZConstants{
          .positions = g_renderer->get_buffer_device_address<glsl::Positions>(
              vertex_pool.positions),
          .transform_matrices = transform_matrices,
          .pv = proj_view,
      });
      u32 batch_id = glsl::get_batch_id(attribute_mask, pool);
      u32 offset = data.batch_offsets[batch_id];
      u32 count = data.batch_max_counts[batch_id];
      render_pass.draw_indexed_indirect_count(
          commands.slice<glsl::DrawIndexedIndirectCommand>(offset, count),
          batch_counts.slice<u32>(batch_id));
    }
  }
}

struct EarlyZPassConfig {
  Handle<GraphicsPipeline> pipeline;
  glm::uvec2 viewport;
};

void setup_early_z_pass(RgBuilder &rgb, const EarlyZPassConfig &cfg) {
  EarlyZPassResources rcs = {};
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass(EARLY_Z_PASS);

  rcs.commands =
      pass.read_buffer(INDIRECT_COMMANDS_BUFFER, RG_INDIRECT_COMMAND_BUFFER);

  rcs.batch_counts =
      pass.read_buffer(BATCH_COMMAND_COUNTS_BUFFER, RG_INDIRECT_COMMAND_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(TRANSFORM_MATRICES_BUFFER, RG_VS_READ_BUFFER);

  glm::uvec2 viewport = cfg.viewport;

  pass.create_depth_attachment(
      {
          .name = DEPTH_BUFFER,
          .format = DEPTH_FORMAT,
          .width = viewport.x,
          .height = viewport.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_update_callback(ren_rg_update_callback(EarlyZPassData) {
    return viewport == data.viewport;
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(EarlyZPassData) {
    run_early_z_pass(rg, render_pass, rcs, data);
  });
}

struct OpaquePassResources {
  std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      pipelines;
  RgBufferId commands;
  RgBufferId batch_counts;
  RgBufferId uniforms;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  RgTextureId exposure;
  bool early_z = false;
};

struct OpaquePassData {
  Span<const u32> batch_offsets;
  Span<const u32> batch_max_counts;
  Span<const VertexPoolList> vertex_pool_lists;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  glm::uvec2 viewport;
  glm::mat4 pv;
  glm::vec3 eye;
  u32 num_directional_lights = 0;
};

void run_opaque_pass(const RgRuntime &rg, RenderPass &render_pass,
                     const OpaquePassResources &rcs,
                     const OpaquePassData &data) {
  if (data.mesh_instances.empty()) {
    return;
  }

  const BufferView &materials = rg.get_buffer(rcs.materials);
  const BufferView &mesh_instances = rg.get_buffer(rcs.mesh_instances);
  const BufferView &transform_matrices = rg.get_buffer(rcs.transform_matrices);
  const BufferView &normal_matrices = rg.get_buffer(rcs.normal_matrices);
  const BufferView &directional_lights = rg.get_buffer(rcs.directional_lights);
  const BufferView &commands = rg.get_buffer(rcs.commands);
  const BufferView &batch_counts = rg.get_buffer(rcs.batch_counts);
  StorageTextureId exposure = rg.get_storage_texture_descriptor(rcs.exposure);

  auto *uniforms = rg.map_buffer<glsl::OpaqueUniformBuffer>(rcs.uniforms);
  *uniforms = {
      .materials =
          g_renderer->get_buffer_device_address<glsl::Materials>(materials),
      .mesh_instances =
          g_renderer->get_buffer_device_address<glsl::DrawMeshInstances>(
              mesh_instances),
      .transform_matrices =
          g_renderer->get_buffer_device_address<glsl::TransformMatrices>(
              transform_matrices),
      .normal_matrices =
          g_renderer->get_buffer_device_address<glsl::NormalMatrices>(
              normal_matrices),
      .directional_lights =
          g_renderer->get_buffer_device_address<glsl::DirectionalLights>(
              directional_lights),
      .num_directional_lights = data.num_directional_lights,
      .pv = data.pv,
      .eye = data.eye,
      .exposure_texture = exposure,
  };

  auto ub = g_renderer->get_buffer_device_address<glsl::OpaqueUniformBuffer>(
      rg.get_buffer(rcs.uniforms));

  if (rcs.early_z) {
    render_pass.set_depth_compare_op(VK_COMPARE_OP_EQUAL);
  } else {
    render_pass.set_depth_compare_op(VK_COMPARE_OP_GREATER);
  }

  for (u32 attribute_mask = 0; attribute_mask < glsl::NUM_MESH_ATTRIBUTE_FLAGS;
       ++attribute_mask) {
    const VertexPoolList &vertex_pool_list =
        data.vertex_pool_lists[attribute_mask];
    if (vertex_pool_list.empty()) {
      continue;
    }
    render_pass.bind_graphics_pipeline(rcs.pipelines[attribute_mask]);
    render_pass.bind_descriptor_sets({rg.get_texture_set()});
    for (const auto &[pool, vertex_pool] : vertex_pool_list | enumerate) {
      render_pass.bind_index_buffer(vertex_pool.indices, VK_INDEX_TYPE_UINT32);
      render_pass.set_push_constants(glsl::OpaqueConstants{
          .positions = g_renderer->get_buffer_device_address<glsl::Positions>(
              vertex_pool.positions),
          .normals = g_renderer->get_buffer_device_address<glsl::Normals>(
              vertex_pool.normals),
          .tangents = g_renderer->try_get_buffer_device_address<glsl::Tangents>(
              vertex_pool.tangents),
          .uvs = g_renderer->try_get_buffer_device_address<glsl::UVs>(
              vertex_pool.uvs),
          .colors = g_renderer->try_get_buffer_device_address<glsl::Colors>(
              vertex_pool.colors),
          .ub = ub,
      });
      u32 batch_id = glsl::get_batch_id(attribute_mask, pool);
      u32 offset = data.batch_offsets[batch_id];
      u32 count = data.batch_max_counts[batch_id];
      render_pass.draw_indexed_indirect_count(
          commands.slice<glsl::DrawIndexedIndirectCommand>(offset, count),
          batch_counts.slice<u32>(batch_id));
    }
  }
}

struct OpaquePassConfig {
  std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      pipelines;
  ExposurePassOutput exposure;
  glm::uvec2 viewport;
  bool early_z = false;
};

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg) {
  OpaquePassResources rcs;
  rcs.pipelines = cfg.pipelines;

  auto pass = rgb.create_pass(OPAQUE_PASS);

  rcs.commands =
      pass.read_buffer(INDIRECT_COMMANDS_BUFFER, RG_INDIRECT_COMMAND_BUFFER);

  rcs.batch_counts =
      pass.read_buffer(BATCH_COMMAND_COUNTS_BUFFER, RG_INDIRECT_COMMAND_BUFFER);

  rcs.uniforms = pass.create_buffer(
      {
          .name = "opaque-pass-uniforms",
          .size = sizeof(glsl::OpaqueUniformBuffer),
      },
      RG_HOST_WRITE_BUFFER | RG_VS_READ_BUFFER | RG_FS_READ_BUFFER);

  rcs.materials = pass.read_buffer(MATERIALS_BUFFER, RG_FS_READ_BUFFER);

  rcs.mesh_instances =
      pass.read_buffer(MESH_INSTANCE_DRAW_DATA_BUFFER, RG_VS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(TRANSFORM_MATRICES_BUFFER, RG_VS_READ_BUFFER);

  rcs.normal_matrices =
      pass.read_buffer(NORMAL_MATRICES_BUFFER, RG_VS_READ_BUFFER);

  rcs.directional_lights =
      pass.read_buffer(DIRECTIONAL_LIGHTS_BUFFER, RG_FS_READ_BUFFER);

  rcs.exposure = pass.read_texture("exposure", RG_FS_READ_TEXTURE,
                                   cfg.exposure.temporal_layer);

  glm::uvec2 viewport = cfg.viewport;

  pass.create_color_attachment(
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

  rcs.early_z = cfg.early_z;
  if (cfg.early_z) {
    pass.read_depth_attachment(DEPTH_BUFFER);
  } else {
    pass.create_depth_attachment(
        {
            .name = DEPTH_BUFFER,
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

  pass.set_update_callback(ren_rg_update_callback(OpaquePassData) {
    return viewport == data.viewport;
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(OpaquePassData) {
    run_opaque_pass(rg, render_pass, rcs, data);
  });
}

} // namespace

void setup_opaque_passes(RgBuilder &rgb, const OpaquePassesConfig &cfg) {
  setup_upload_pass(rgb);
  setup_instance_culling_pass(rgb,
                              {.pipeline = cfg.pipelines->instance_culling});
  if (cfg.early_z) {
    setup_early_z_pass(rgb, {
                                .pipeline = cfg.pipelines->early_z_pass,
                                .viewport = cfg.viewport,
                            });
  }
  setup_opaque_pass(rgb, {
                             .pipelines = cfg.pipelines->opaque_pass,
                             .exposure = cfg.exposure,
                             .viewport = cfg.viewport,
                             .early_z = cfg.early_z,
                         });
}

auto set_opaque_passes_data(RenderGraph &rg, const OpaquePassesData &data)
    -> bool {
  bool valid = true;

  valid = rg.set_pass_data(UPLOAD_PASS,
                           UploadPassData{
                               .batch_offsets = data.batch_offsets,
                               .meshes = data.meshes,
                               .materials = data.materials,
                               .mesh_instances = data.mesh_instances,
                               .directional_lights = data.directional_lights,
                           });
  if (not valid) {
    return false;
  }

  glm::mat4 pv = data.proj * data.view;

  valid = rg.set_pass_data(
      INSTANCE_CULLING_PASS,
      InstanceCullingPassData{
          .num_mesh_instances = u32(data.mesh_instances.size()),
          .pv = pv,
          .frustum_culling = data.instance_frustum_culling,
      });
  if (not valid) {
    return false;
  }

  if (data.early_z) {
    valid = rg.set_pass_data(EARLY_Z_PASS,
                             EarlyZPassData{
                                 .batch_offsets = data.batch_offsets,
                                 .batch_max_counts = data.batch_max_counts,
                                 .vertex_pool_lists = data.vertex_pool_lists,
                                 .meshes = data.meshes,
                                 .mesh_instances = data.mesh_instances,
                                 .viewport = data.viewport,
                                 .proj = data.proj,
                                 .view = data.view,
                                 .eye = data.eye,
                             });
  } else {
    valid = not rg.is_pass_valid(EARLY_Z_PASS);
  }
  if (not valid) {
    return false;
  }

  valid = rg.set_pass_data(
      OPAQUE_PASS,
      OpaquePassData{
          .batch_offsets = data.batch_offsets,
          .batch_max_counts = data.batch_max_counts,
          .vertex_pool_lists = data.vertex_pool_lists,
          .meshes = data.meshes,
          .mesh_instances = data.mesh_instances,
          .viewport = data.viewport,
          .pv = pv,
          .eye = data.eye,
          .num_directional_lights = u32(data.directional_lights.size()),
      });
  if (not valid) {
    return false;
  }

  return true;
}

} // namespace ren
