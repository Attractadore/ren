#include "Passes/Opaque.hpp"
#include "Camera.inl"
#include "CommandRecorder.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/Batch.hpp"
#include "glsl/EarlyZPass.hpp"
#include "glsl/InstanceCullingAndLOD.hpp"
#include "glsl/OpaquePass.hpp"

#include <range/v3/view.hpp>

namespace ren {

namespace {

const char UPLOAD_PASS[] = "upload";
const char EARLY_Z_PASS[] = "early-z";
const char OPAQUE_PASS[] = "opaque";
const char INSTANCE_CULLING_AND_LOD_INIT_PASS[] =
    "init-instance-culling-and-lod";
const char INSTANCE_CULLING_AND_LOD_PASS[] = "instance-culling-and-lod";

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

const char SCENE_DATA[] = "scene-data";
const char BATCHES_DATA[] = "batches-data";

struct SceneData {
  u32 num_mesh_instances = 0;
  u32 num_directional_lights = 0;
  glm::mat4 pv;
  glm::vec3 eye;
};

struct BatchData {
  Vector<u32> counts;
  Vector<u32> offsets;
};

struct UploadPassResources {
  RgParameterId scene_cfg;

  RgRWVariableId scene;
  RgRWVariableId batches;

  RgBufferId materials;
  RgBufferId mesh_instance_draw_data;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;

  RgBufferId mesh_cull_data;
  RgBufferId mesh_instance_cull_data;
  RgBufferId batch_command_offsets;

  glm::uvec2 viewport;
};

void run_upload_pass(const RgRuntime &rg, const UploadPassResources &rcs) {
  assert(rcs.materials);
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);

  const auto &scene_cfg = rg.get_parameter<SceneRuntimeConfig>(rcs.scene_cfg);

  glm::mat4 proj = get_projection_matrix(scene_cfg.camera, rcs.viewport);
  glm::mat4 view = get_view_matrix(scene_cfg.camera);

  rg.get_variable<SceneData>(rcs.scene) = {
      .num_mesh_instances = u32(scene_cfg.mesh_instances.size()),
      .num_directional_lights = u32(scene_cfg.directional_lights.size()),
      .pv = proj * view,
      .eye = scene_cfg.camera.position,
  };

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  ranges::copy(scene_cfg.materials, materials);

  auto *mesh_cull_data = rg.map_buffer<glsl::MeshCullData>(rcs.mesh_cull_data);
  for (const auto &[i, mesh] : scene_cfg.meshes | enumerate) {
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
  for (const auto &[i, mesh_instance] : scene_cfg.mesh_instances | enumerate) {
    const Mesh &mesh = scene_cfg.meshes[mesh_instance.mesh];
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

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  ranges::copy(scene_cfg.directional_lights, directional_lights);

  auto &batches = rg.get_variable<BatchData>(rcs.batches);
  {
    batches.counts.resize(glsl::NUM_BATCHES);
    ranges::fill(batches.counts, 0);
    for (const MeshInstance &mesh_instance : scene_cfg.mesh_instances) {
      const Mesh &mesh = scene_cfg.meshes[mesh_instance.mesh];
      u32 batch_id = glsl::get_batch_id(
          static_cast<uint32_t>(mesh.attributes.get()), mesh.pool);
      batches.counts[batch_id]++;
    }
    batches.offsets.assign(batches.counts | ranges::views::exclusive_scan(0));
  }
  auto *batch_offsets = rg.map_buffer<u32>(rcs.batch_command_offsets);
  ranges::copy(batches.offsets, batch_offsets);
}

struct UploadPassConfig {
  u32 num_meshes;
  u32 num_mesh_instances;
  u32 num_materials;
  u32 num_directional_lights;
  glm::uvec2 viewport;
};

void setup_upload_pass(RgBuilder &rgb, const UploadPassConfig &cfg) {
  auto pass = rgb.create_pass(UPLOAD_PASS);

  UploadPassResources rcs;

  rgb.create_parameter<SceneRuntimeConfig>(SCENE_RUNTIME_CONFIG);

  rcs.scene_cfg = pass.read_parameter(SCENE_RUNTIME_CONFIG);

  rcs.scene = pass.create_variable<SceneData>(SCENE_DATA);

  rcs.batches = pass.create_variable<BatchData>(BATCHES_DATA);

  rcs.materials = pass.create_buffer(
      {
          .name = MATERIALS_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::Material) * cfg.num_materials,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.mesh_cull_data = pass.create_buffer(
      {
          .name = MESH_CULL_DATA_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::MeshCullData) * cfg.num_meshes,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.mesh_instance_cull_data = pass.create_buffer(
      {
          .name = MESH_INSTANCE_CULL_DATA_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::MeshInstanceCullData) * cfg.num_mesh_instances,
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
      {
          .name = MESH_INSTANCE_DRAW_DATA_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::MeshInstanceDrawData) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.transform_matrices = pass.create_buffer(
      {
          .name = TRANSFORM_MATRICES_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::mat4x3) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.normal_matrices = pass.create_buffer(
      {
          .name = NORMAL_MATRICES_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glm::mat3) * cfg.num_mesh_instances,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.directional_lights = pass.create_buffer(
      {
          .name = DIRECTIONAL_LIGHTS_BUFFER,
          .heap = BufferHeap::Dynamic,
          .size = sizeof(glsl::DirLight) * cfg.num_directional_lights,
      },
      RG_HOST_WRITE_BUFFER);

  rcs.viewport = cfg.viewport;

  pass.set_host_callback(
      [=](const RgRuntime &rt) { run_upload_pass(rt, rcs); });
}

struct InstanceCullingAndLODPassResources {
  Handle<ComputePipeline> pipeline;
  RgVariableId scene;
  RgParameterId cfg;
  RgBufferId meshes;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId commands;
  RgBufferId batch_offsets;
  RgBufferId batch_counts;
  glm::uvec2 viewport;
};

void run_instance_culling_and_lod_pass(
    const RgRuntime &rg, ComputePass &pass,
    const InstanceCullingAndLODPassResources &rcs) {
  const auto &scene = rg.get_variable<SceneData>(rcs.scene);

  const auto &cfg =
      rg.get_parameter<InstanceCullingAndLODRuntimeConfig>(rcs.cfg);

  u32 mask = 0;
  if (cfg.frustum_culling) {
    mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
  }
  if (cfg.lod_selection) {
    mask |= glsl::INSTANCE_CULLING_AND_LOD_SELECTION_BIT;
  }

  float num_viewport_triangles =
      rcs.viewport.x * rcs.viewport.y / cfg.lod_triangle_pixels;
  float lod_triangle_density = num_viewport_triangles / 4.0f;

  pass.bind_compute_pipeline(rcs.pipeline);
  pass.set_push_constants(glsl::InstanceCullingAndLODConstants{
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
      .num_mesh_instances = scene.num_mesh_instances,
      .pv = scene.pv,
      .lod_triangle_density = lod_triangle_density,
      .lod_bias = cfg.lod_bias,
  });
  pass.dispatch_threads(scene.num_mesh_instances,
                        glsl::INSTANCE_CULLING_AND_LOD_THREADS);
}

struct InstanceCullingAndLODPassConfig {
  Handle<ComputePipeline> pipeline;
  u32 num_mesh_instances;
  glm::uvec2 viewport;
};

void setup_instance_culling_and_lod_pass(
    RgBuilder &rgb, const InstanceCullingAndLODPassConfig &cfg) {
  InstanceCullingAndLODPassResources rcs;
  rcs.pipeline = cfg.pipeline;

  String batch_counts_empty =
      fmt::format("{}-empty", BATCH_COMMAND_COUNTS_BUFFER);
  {
    auto init_pass = rgb.create_pass(INSTANCE_CULLING_AND_LOD_INIT_PASS);

    RgBufferId batch_counts = init_pass.create_buffer(
        {
            .name = batch_counts_empty,
            .heap = BufferHeap::Static,
            .size = sizeof(u32[glsl::NUM_BATCHES]),
        },
        RG_TRANSFER_DST_BUFFER);

    init_pass.set_transfer_callback(
        [=](const RgRuntime &rt, TransferPass &cmd) {
          cmd.fill_buffer(rt.get_buffer(batch_counts), 0);
        });
  }

  auto pass = rgb.create_pass(INSTANCE_CULLING_AND_LOD_PASS);

  rcs.scene = pass.read_variable(SCENE_DATA);

  rgb.create_parameter<InstanceCullingAndLODRuntimeConfig>(
      INSTANCE_CULLING_AND_LOD_RUNTIME_CONFIG);

  rcs.cfg = pass.read_parameter(INSTANCE_CULLING_AND_LOD_RUNTIME_CONFIG);

  rcs.meshes = pass.read_buffer(MESH_CULL_DATA_BUFFER, RG_CS_READ_BUFFER);

  rcs.mesh_instances =
      pass.read_buffer(MESH_INSTANCE_CULL_DATA_BUFFER, RG_CS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(TRANSFORM_MATRICES_BUFFER, RG_CS_READ_BUFFER);

  rcs.commands = pass.create_buffer(
      {
          .name = INDIRECT_COMMANDS_BUFFER,
          .heap = BufferHeap::Static,
          .size =
              sizeof(glsl::DrawIndexedIndirectCommand) * cfg.num_mesh_instances,
      },
      RG_CS_WRITE_BUFFER);

  rcs.batch_offsets =
      pass.read_buffer(BATCH_COMMAND_OFFSETS_BUFFER, RG_CS_READ_BUFFER);

  rcs.batch_counts =
      pass.write_buffer(BATCH_COMMAND_COUNTS_BUFFER, batch_counts_empty,
                        RG_CS_READ_BUFFER | RG_CS_WRITE_BUFFER);

  rcs.viewport = cfg.viewport;

  pass.set_compute_callback([=](const RgRuntime &rt, ComputePass &pass) {
    run_instance_culling_and_lod_pass(rt, pass, rcs);
  });
};

struct EarlyZPassResources {
  Handle<GraphicsPipeline> pipeline;
  RgParameterId scene_cfg;
  RgVariableId scene;
  RgVariableId batches;
  RgBufferId commands;
  RgBufferId batch_counts;
  RgBufferId transform_matrices;
};

void run_early_z_pass(const RgRuntime &rg, RenderPass &render_pass,
                      const EarlyZPassResources &rcs) {
  const auto &scene_cfg = rg.get_parameter<SceneRuntimeConfig>(rcs.scene_cfg);
  const auto &scene = rg.get_variable<SceneData>(rcs.scene);
  const auto &batches = rg.get_variable<BatchData>(rcs.batches);

  if (scene.num_mesh_instances == 0) {
    return;
  }

  const BufferView &commands = rg.get_buffer(rcs.commands);
  const BufferView &batch_counts = rg.get_buffer(rcs.batch_counts);
  auto transform_matrices =
      g_renderer->get_buffer_device_address<glsl::TransformMatrices>(
          rg.get_buffer(rcs.transform_matrices));

  render_pass.bind_graphics_pipeline(rcs.pipeline);
  for (u32 attribute_mask = 0; attribute_mask < glsl::NUM_MESH_ATTRIBUTE_FLAGS;
       ++attribute_mask) {
    for (const auto &[pool, vertex_pool] :
         scene_cfg.vertex_pool_lists[attribute_mask] | enumerate) {
      render_pass.bind_index_buffer(vertex_pool.indices, VK_INDEX_TYPE_UINT32);
      render_pass.set_push_constants(glsl::EarlyZConstants{
          .positions = g_renderer->get_buffer_device_address<glsl::Positions>(
              vertex_pool.positions),
          .transform_matrices = transform_matrices,
          .pv = scene.pv,
      });
      u32 batch_id = glsl::get_batch_id(attribute_mask, pool);
      u32 offset = batches.offsets[batch_id];
      u32 count = batches.counts[batch_id];
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

  rcs.scene_cfg = pass.read_parameter(SCENE_RUNTIME_CONFIG);

  rcs.scene = pass.read_variable(SCENE_DATA);

  rcs.batches = pass.read_variable(BATCHES_DATA);

  rcs.commands =
      pass.read_buffer(INDIRECT_COMMANDS_BUFFER, RG_INDIRECT_COMMAND_BUFFER);

  rcs.batch_counts =
      pass.read_buffer(BATCH_COMMAND_COUNTS_BUFFER, RG_INDIRECT_COMMAND_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer(TRANSFORM_MATRICES_BUFFER, RG_VS_READ_BUFFER);

  pass.create_depth_attachment(
      {
          .name = DEPTH_BUFFER,
          .format = DEPTH_FORMAT,
          .width = cfg.viewport.x,
          .height = cfg.viewport.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_graphics_callback([=](const RgRuntime &rt, RenderPass &render_pass) {
    run_early_z_pass(rt, render_pass, rcs);
  });
}

struct OpaquePassResources {
  std::array<Handle<GraphicsPipeline>, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      pipelines;
  RgParameterId scene_cfg;
  RgVariableId scene;
  RgVariableId batches;
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

void run_opaque_pass(const RgRuntime &rg, RenderPass &render_pass,
                     const OpaquePassResources &rcs) {
  const auto &scene_cfg = rg.get_parameter<SceneRuntimeConfig>(rcs.scene_cfg);
  const auto &scene = rg.get_variable<SceneData>(rcs.scene);
  const auto &batches = rg.get_variable<BatchData>(rcs.batches);

  if (scene.num_mesh_instances == 0) {
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
      .num_directional_lights = scene.num_directional_lights,
      .pv = scene.pv,
      .eye = scene.eye,
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
        scene_cfg.vertex_pool_lists[attribute_mask];
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
      u32 offset = batches.offsets[batch_id];
      u32 count = batches.counts[batch_id];
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

  rcs.scene_cfg = pass.read_parameter(SCENE_RUNTIME_CONFIG);

  rcs.scene = pass.read_variable(SCENE_DATA);

  rcs.batches = pass.read_variable(BATCHES_DATA);

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

  pass.create_color_attachment(
      {
          .name = "hdr",
          .format = HDR_FORMAT,
          .width = cfg.viewport.x,
          .height = cfg.viewport.y,
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
            .width = cfg.viewport.x,
            .height = cfg.viewport.y,
        },
        {
            .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .store = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clear_depth = 0.0f,
        });
  }

  pass.set_graphics_callback([=](const RgRuntime &rt, RenderPass &render_pass) {
    run_opaque_pass(rt, render_pass, rcs);
  });
}

} // namespace

void setup_opaque_passes(RgBuilder &rgb, const OpaquePassesConfig &cfg) {
  setup_upload_pass(rgb,
                    UploadPassConfig{
                        .num_meshes = cfg.num_meshes,
                        .num_mesh_instances = cfg.num_mesh_instances,
                        .num_materials = cfg.num_materials,
                        .num_directional_lights = cfg.num_directional_lights,
                        .viewport = cfg.viewport,
                    });

  setup_instance_culling_and_lod_pass(
      rgb, InstanceCullingAndLODPassConfig{
               .pipeline = cfg.pipelines->instance_culling_and_lod,
               .num_mesh_instances = cfg.num_mesh_instances,
               .viewport = cfg.viewport,
           });

  if (cfg.early_z) {
    setup_early_z_pass(rgb, EarlyZPassConfig{
                                .pipeline = cfg.pipelines->early_z_pass,
                                .viewport = cfg.viewport,
                            });
  }

  setup_opaque_pass(rgb, OpaquePassConfig{
                             .pipelines = cfg.pipelines->opaque_pass,
                             .exposure = cfg.exposure,
                             .viewport = cfg.viewport,
                             .early_z = cfg.early_z,
                         });
}

} // namespace ren
