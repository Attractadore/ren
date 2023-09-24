#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "Mesh.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/OpaquePass.hpp"

namespace ren {

namespace {

struct OpaquePassResources {
  Handle<GraphicsPipeline> pipeline;
  RgBufferId uniforms;
  RgBufferId meshes;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  RgTextureId exposure;
};

void run_opaque_pass(const RgRuntime &rg, RenderPass &render_pass,
                     const OpaquePassResources &rcs,
                     const OpaquePassData &data) {
  if (data.mesh_instances.empty()) {
    return;
  }

  const BufferView &meshes = rg.get_buffer(rcs.meshes);
  const BufferView &materials = rg.get_buffer(rcs.materials);
  const BufferView &mesh_instances = rg.get_buffer(rcs.mesh_instances);
  const BufferView &transform_matrices = rg.get_buffer(rcs.transform_matrices);
  const BufferView &normal_matrices = rg.get_buffer(rcs.normal_matrices);
  const BufferView &directional_lights = rg.get_buffer(rcs.directional_lights);
  StorageTextureId exposure = rg.get_storage_texture_descriptor(rcs.exposure);

  auto *uniforms = rg.map_buffer<glsl::OpaqueUniformBuffer>(rcs.uniforms);
  *uniforms = {
      .positions = g_device->get_buffer_device_address<glsl::Positions>(
          data.vertex_positions),
      .normals = g_device->get_buffer_device_address<glsl::Normals>(
          data.vertex_normals),
      .colors =
          g_device->get_buffer_device_address<glsl::Colors>(data.vertex_colors),
      .uvs = g_device->get_buffer_device_address<glsl::UVs>(data.vertex_uvs),
      .meshes = g_device->get_buffer_device_address<glsl::Meshes>(meshes),
      .materials =
          g_device->get_buffer_device_address<glsl::Materials>(materials),
      .mesh_instances =
          g_device->get_buffer_device_address<glsl::MeshInstances>(
              mesh_instances),
      .transform_matrices =
          g_device->get_buffer_device_address<glsl::TransformMatrices>(
              transform_matrices),
      .normal_matrices =
          g_device->get_buffer_device_address<glsl::NormalMatrices>(
              normal_matrices),
      .directional_lights =
          g_device->get_buffer_device_address<glsl::DirectionalLights>(
              directional_lights),
      .num_directional_lights = data.num_dir_lights,
      .pv = data.proj * data.view,
      .eye = data.eye,
      .exposure_texture = exposure,
  };

  render_pass.bind_graphics_pipeline(rcs.pipeline);
  render_pass.bind_descriptor_sets({rg.get_texture_set()});

  auto ub = g_device->get_buffer_device_address<glsl::OpaqueUniformBuffer>(
      rg.get_buffer(rcs.uniforms));
  render_pass.bind_index_buffer(data.vertex_indices, VK_INDEX_TYPE_UINT32);
  render_pass.set_push_constants(glsl::OpaqueConstants{.ub = ub});

  for (const auto &&[index, mesh_instance] : enumerate(data.mesh_instances)) {
    const Mesh &mesh = data.meshes[mesh_instance.mesh];
    render_pass.draw_indexed({
        .num_indices = mesh.num_indices,
        .num_instances = 1,
        .first_index = mesh.base_index,
        .vertex_offset = i32(mesh.base_vertex),
        .first_instance = u32(index),
    });
  }
}

} // namespace

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg) {
  assert(cfg.pipeline);

  OpaquePassResources rcs;
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass("opaque");

  rcs.uniforms = pass.create_buffer(
      {
          .name = "opaque-pass-uniforms",
          .size = sizeof(glsl::OpaqueUniformBuffer),
      },
      RG_HOST_WRITE_BUFFER | RG_VS_READ_BUFFER | RG_FS_READ_BUFFER);

  rcs.meshes = pass.read_buffer("meshes", RG_VS_READ_BUFFER);

  rcs.materials = pass.read_buffer("materials", RG_FS_READ_BUFFER);

  rcs.mesh_instances = pass.read_buffer("mesh-instances", RG_VS_READ_BUFFER);

  rcs.transform_matrices =
      pass.read_buffer("transform-matrices", RG_VS_READ_BUFFER);

  rcs.normal_matrices = pass.read_buffer("normal-matrices", RG_VS_READ_BUFFER);

  rcs.directional_lights =
      pass.read_buffer("directional-lights", RG_FS_READ_BUFFER);

  rcs.exposure = pass.read_texture("exposure", RG_FS_READ_TEXTURE,
                                   cfg.exposure.temporal_layer);

  glm::uvec2 viewport_size = cfg.viewport_size;

  auto texture = pass.create_color_attachment(
      {
          .name = "color-buffer",
          .format = COLOR_FORMAT,
          .width = viewport_size.x,
          .height = viewport_size.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
      });

  auto depth_texture = pass.create_depth_attachment(
      {
          .name = "depth-buffer",
          .format = DEPTH_FORMAT,
          .width = viewport_size.x,
          .height = viewport_size.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_update_callback(ren_rg_update_callback(OpaquePassData) {
    return viewport_size == data.viewport_size;
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(OpaquePassData) {
    run_opaque_pass(rg, render_pass, rcs, data);
  });
}

} // namespace ren
