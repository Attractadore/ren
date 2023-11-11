#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.hpp"
#include "glsl/OpaquePass.hpp"

namespace ren {

namespace {

const char UPLOAD_PASS[] = "upload";
const char EARLY_Z_PASS[] = "early-z";
const char OPAQUE_PASS[] = "opaque";

const char MATERIALS_BUFFER[] = "materials";
const char MESH_INSTANCES_BUFFER[] = "mesh-instances";
const char TRANSFORM_MATRICES_BUFFER[] = "transform-matrices";
const char NORMAL_MATRICES_BUFFER[] = "normal-matrices";
const char DIRECTIONAL_LIGHTS_BUFFER[] = "directional-lights";

const char DEPTH_BUFFER[] = "depth-buffer";

struct UploadPassResources {
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
};

struct UploadPassData {
  Span<const Mesh> meshes;
  Span<const glsl::Material> materials;
  Span<const MeshInstance> mesh_instances;
  Span<const glsl::DirLight> directional_lights;
};

void run_upload_pass(const RgRuntime &rg, const UploadPassResources &rcs,
                     const UploadPassData &data) {
  assert(rcs.materials);
  assert(rcs.mesh_instances);
  assert(rcs.transform_matrices);
  assert(rcs.normal_matrices);
  assert(rcs.directional_lights);

  auto *materials = rg.map_buffer<glsl::Material>(rcs.materials);
  ranges::copy(data.materials, materials);

  auto *mesh_instances = rg.map_buffer<glsl::MeshInstance>(rcs.mesh_instances);
  auto *transform_matrices = rg.map_buffer<glm::mat4x3>(rcs.transform_matrices);
  auto *normal_matrices = rg.map_buffer<glm::mat3>(rcs.normal_matrices);
  for (const auto &[i, mesh_instance] : data.mesh_instances | enumerate) {
    const Mesh &mesh = data.meshes[mesh_instance.mesh];
    mesh_instances[i] = {
        .tbs = mesh.tbs,
        .material = mesh_instance.material,
    };
    transform_matrices[i] = mesh_instance.matrix;
    normal_matrices[i] =
        glm::transpose(glm::inverse(glm::mat3(mesh_instance.matrix)));
  }

  auto *directional_lights =
      rg.map_buffer<glsl::DirLight>(rcs.directional_lights);
  ranges::copy(data.directional_lights, directional_lights);
}

void setup_upload_pass(RgBuilder &rgb) {
  auto pass = rgb.create_pass(UPLOAD_PASS);

  UploadPassResources rcs;

  rcs.materials =
      pass.create_buffer({.name = MATERIALS_BUFFER}, RG_HOST_WRITE_BUFFER);

  rcs.mesh_instances =
      pass.create_buffer({.name = MESH_INSTANCES_BUFFER}, RG_HOST_WRITE_BUFFER);

  rcs.transform_matrices = pass.create_buffer(
      {.name = TRANSFORM_MATRICES_BUFFER}, RG_HOST_WRITE_BUFFER);

  rcs.normal_matrices = pass.create_buffer({.name = NORMAL_MATRICES_BUFFER},
                                           RG_HOST_WRITE_BUFFER);

  rcs.directional_lights = pass.create_buffer(
      {.name = DIRECTIONAL_LIGHTS_BUFFER}, RG_HOST_WRITE_BUFFER);

  pass.set_update_callback(ren_rg_update_callback(UploadPassData) {
    rg.resize_buffer(rcs.materials,
                     sizeof(glsl::Material) * data.materials.size());
    rg.resize_buffer(rcs.mesh_instances,
                     sizeof(glsl::MeshInstance) * data.mesh_instances.size());
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

struct EarlyZPassResources {
  Handle<GraphicsPipeline> pipeline;
  RgBufferId transform_matrices;
};

struct EarlyZPassData {
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
  glsl::EarlyZConstants constants = {
      .transform_matrices =
          g_renderer->get_buffer_device_address<glsl::TransformMatrices>(
              rg.get_buffer(rcs.transform_matrices)),
      .pv = data.proj * data.view,
  };
  render_pass.bind_graphics_pipeline(rcs.pipeline);
  for (const auto &[index, mesh_instance] : data.mesh_instances | enumerate) {
    const Mesh &mesh = data.meshes[mesh_instance.mesh];
    const VertexPoolList &vertex_pool_list =
        data.vertex_pool_lists[usize(mesh.attributes.get())];
    const VertexPool &vertex_pool = vertex_pool_list[mesh.pool];
    render_pass.bind_index_buffer(vertex_pool.indices, VK_INDEX_TYPE_UINT32);
    constants.positions =
        g_renderer->get_buffer_device_address<glsl::Positions>(
            vertex_pool.positions);
    render_pass.set_push_constants(constants);
    render_pass.draw_indexed({
        .num_indices = mesh.num_indices,
        .num_instances = 1,
        .first_index = mesh.base_index,
        .vertex_offset = i32(mesh.base_vertex),
        .first_instance = u32(index),
    });
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

  rcs.transform_matrices =
      pass.read_buffer(TRANSFORM_MATRICES_BUFFER, RG_VS_READ_BUFFER);

  glm::uvec2 viewport_size = cfg.viewport;

  pass.create_depth_attachment(
      {
          .name = DEPTH_BUFFER,
          .format = DEPTH_FORMAT,
          .width = viewport_size.x,
          .height = viewport_size.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_update_callback(ren_rg_update_callback(EarlyZPassData) {
    return viewport_size == data.viewport;
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(EarlyZPassData) {
    run_early_z_pass(rg, render_pass, rcs, data);
  });
}

struct OpaquePassResources {
  std::array<Handle<GraphicsPipeline>, NUM_MESH_ATTRIBUTE_FLAGS> pipelines;
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
  Span<const VertexPoolList> vertex_pool_lists;
  Span<const Mesh> meshes;
  Span<const MeshInstance> mesh_instances;
  glm::uvec2 viewport;
  glm::mat4 proj;
  glm::mat4 view;
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
  StorageTextureId exposure = rg.get_storage_texture_descriptor(rcs.exposure);

  auto *uniforms = rg.map_buffer<glsl::OpaqueUniformBuffer>(rcs.uniforms);
  *uniforms = {
      .materials =
          g_renderer->get_buffer_device_address<glsl::Materials>(materials),
      .mesh_instances =
          g_renderer->get_buffer_device_address<glsl::MeshInstances>(
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
      .pv = data.proj * data.view,
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

  for (const auto &&[index, mesh_instance] : enumerate(data.mesh_instances)) {
    const Mesh &mesh = data.meshes[mesh_instance.mesh];
    auto attributes = static_cast<usize>(mesh.attributes.get());
    const VertexPoolList &vertex_pool_list =
        data.vertex_pool_lists[usize(mesh.attributes.get())];
    const VertexPool &vertex_pool = vertex_pool_list[mesh.pool];
    render_pass.bind_graphics_pipeline(rcs.pipelines[attributes]);
    render_pass.bind_descriptor_sets({rg.get_texture_set()});
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
    render_pass.draw_indexed({
        .num_indices = mesh.num_indices,
        .num_instances = 1,
        .first_index = mesh.base_index,
        .vertex_offset = i32(mesh.base_vertex),
        .first_instance = u32(index),
    });
  }
}

struct OpaquePassConfig {
  std::array<Handle<GraphicsPipeline>, NUM_MESH_ATTRIBUTE_FLAGS> pipelines;
  ExposurePassOutput exposure;
  glm::uvec2 viewport;
  bool early_z = false;
};

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg) {
  OpaquePassResources rcs;
  rcs.pipelines = cfg.pipelines;

  auto pass = rgb.create_pass(OPAQUE_PASS);

  rcs.uniforms = pass.create_buffer(
      {
          .name = "opaque-pass-uniforms",
          .size = sizeof(glsl::OpaqueUniformBuffer),
      },
      RG_HOST_WRITE_BUFFER | RG_VS_READ_BUFFER | RG_FS_READ_BUFFER);

  rcs.materials = pass.read_buffer(MATERIALS_BUFFER, RG_FS_READ_BUFFER);

  rcs.mesh_instances =
      pass.read_buffer(MESH_INSTANCES_BUFFER, RG_VS_READ_BUFFER);

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
                               .meshes = data.meshes,
                               .materials = data.materials,
                               .mesh_instances = data.mesh_instances,
                               .directional_lights = data.directional_lights,
                           });
  if (not valid) {
    return false;
  }

  if (data.early_z) {
    valid = rg.set_pass_data(EARLY_Z_PASS,
                             EarlyZPassData{
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
          .vertex_pool_lists = data.vertex_pool_lists,
          .meshes = data.meshes,
          .mesh_instances = data.mesh_instances,
          .viewport = data.viewport,
          .proj = data.proj,
          .view = data.view,
          .eye = data.eye,
          .num_directional_lights = u32(data.directional_lights.size()),
      });
  if (not valid) {
    return false;
  }

  return true;
}

} // namespace ren
