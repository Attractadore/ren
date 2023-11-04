#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "Mesh.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/OpaquePass.hpp"

namespace ren {

namespace {

struct OpaquePassResources {
  std::array<Handle<GraphicsPipeline>, NUM_MESH_ATTRIBUTE_FLAGS> pipelines;
  RgBufferId uniforms;
  RgBufferId materials;
  RgBufferId mesh_instances;
  RgBufferId transform_matrices;
  RgBufferId normal_matrices;
  RgBufferId directional_lights;
  RgTextureId exposure;
  bool early_z : 1 = true;
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
      .num_directional_lights = data.num_dir_lights,
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

} // namespace

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg) {
  assert(cfg.pipeline);

  OpaquePassResources rcs;
  rcs.pipelines = cfg.pipelines;

  auto pass = rgb.create_pass("opaque");

  rcs.uniforms = pass.create_buffer(
      {
          .name = "opaque-pass-uniforms",
          .size = sizeof(glsl::OpaqueUniformBuffer),
      },
      RG_HOST_WRITE_BUFFER | RG_VS_READ_BUFFER | RG_FS_READ_BUFFER);

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

  pass.create_color_attachment(
      {
          .name = "hdr",
          .format = HDR_FORMAT,
          .width = viewport_size.x,
          .height = viewport_size.y,
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
      });

  rcs.early_z = rgb.is_texture_valid("depth-buffer-after-early-z");
  if (rcs.early_z) {
    pass.read_depth_attachment("depth-buffer-after-early-z");
  } else {
    pass.create_depth_attachment(
        {
            .name = "depth-buffer",
            .format = DEPTH_FORMAT,
            .width = viewport_size.x,
            .height = viewport_size.y,
        },
        {
            .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .store = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clear_depth = 0.0f,
        });
  }

  pass.set_update_callback(ren_rg_update_callback(OpaquePassData) {
    return viewport_size == data.viewport_size;
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(OpaquePassData) {
    run_opaque_pass(rg, render_pass, rcs, data);
  });
}

} // namespace ren
