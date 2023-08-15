#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/OpaquePass.hpp"

namespace ren {

namespace {

struct OpaquePassResources {
  Handle<GraphicsPipeline> pipeline;
  RgRtBuffer uniforms;
  RgRtBuffer transform_matrices;
  RgRtBuffer normal_matrices;
  RgRtBuffer directional_lights;
  RgRtBuffer materials;
  RgRtBuffer exposure;
};

void run_opaque_pass(Device &device, const RgRuntime &rg,
                     RenderPass &render_pass, const OpaquePassResources &rcs,
                     const OpaquePassData &data) {
  assert(data.meshes);

  if (data.mesh_insts.empty()) {
    return;
  }

  const auto &transform_matrix_buffer = rg.get_buffer(rcs.transform_matrices);
  const auto &normal_matrix_buffer = rg.get_buffer(rcs.normal_matrices);
  const auto &directional_lights_buffer = rg.get_buffer(rcs.directional_lights);
  const auto &materials_buffer = rg.get_buffer(rcs.materials);
  const auto &exposure_buffer = rg.get_buffer(rcs.exposure);

  auto *uniforms = rg.map_buffer<glsl::OpaqueUniformBuffer>(rcs.uniforms);
  *uniforms = {
      .transform_matrices =
          device.get_buffer_device_address<glsl::TransformMatrices>(
              transform_matrix_buffer),
      .normal_matrices = device.get_buffer_device_address<glsl::NormalMatrices>(
          normal_matrix_buffer),
      .materials =
          device.get_buffer_device_address<glsl::Materials>(materials_buffer),
      .directional_lights =
          device.get_buffer_device_address<glsl::DirectionalLights>(
              directional_lights_buffer),
      .exposure =
          device.get_buffer_device_address<glsl::Exposure>(exposure_buffer),
      .pv = data.proj * data.view,
      .eye = data.eye,
      .num_directional_lights = data.num_dir_lights,
  };

  render_pass.bind_graphics_pipeline(rcs.pipeline);

  auto ub = device.get_buffer_device_address<glsl::OpaqueUniformBuffer>(
      rg.get_buffer(rcs.uniforms));
  for (const auto &&[i, mesh_inst] : enumerate(data.mesh_insts)) {
    const auto &mesh = (*data.meshes)[mesh_inst.mesh];
    auto material = mesh_inst.material;

    auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
    auto positions = device.get_buffer_device_address<glsl::Positions>(
        mesh.vertex_buffer, positions_offset);

    auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
    BufferReference<glsl::Colors> colors;
    if (colors_offset != ATTRIBUTE_UNUSED) {
      colors = device.get_buffer_device_address<glsl::Colors>(
          mesh.vertex_buffer, colors_offset);
    }

    auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
    auto normals = device.get_buffer_device_address<glsl::Normals>(
        mesh.vertex_buffer, normals_offset);

    auto uvs_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_UVS];
    BufferReference<glsl::UVs> uvs;
    if (uvs_offset != ATTRIBUTE_UNUSED) {
      uvs = device.get_buffer_device_address<glsl::UVs>(mesh.vertex_buffer,
                                                        uvs_offset);
    }

    render_pass.set_push_constants(glsl::OpaqueConstants{
        .ub = ub,
        .positions = positions,
        .colors = colors,
        .normals = normals,
        .uvs = uvs,
        .matrix = unsigned(i),
        .material = material,
    });

    render_pass.bind_index_buffer(mesh.index_buffer, mesh.index_format);
    render_pass.draw_indexed({.num_indices = mesh.num_indices});
  }
}

} // namespace

void setup_opaque_pass(RgBuilder &rgb, const OpaquePassConfig &cfg) {
  assert(cfg.pipeline);

  OpaquePassResources rcs;
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass("opaque");

  rcs.transform_matrices =
      pass.read_buffer("transform-matrices", RG_VS_READ_BUFFER);
  rcs.normal_matrices = pass.read_buffer("normal-matrices", RG_VS_READ_BUFFER);

  rcs.directional_lights =
      pass.read_buffer("directional-lights", RG_FS_READ_BUFFER);

  rcs.materials = pass.read_buffer("materials", RG_FS_READ_BUFFER);

  rcs.exposure = pass.read_buffer("exposure", RG_FS_READ_BUFFER,
                                  cfg.exposure.temporal_layer);

  rcs.uniforms = pass.create_buffer(
      {
          .name = "opaque-pass-uniforms",
          .size = sizeof(glsl::OpaqueUniformBuffer),
      },
      RG_VS_READ_BUFFER | RG_FS_READ_BUFFER);

  auto texture = pass.create_color_attachment(
      {
          .name = "color-buffer",
          .format = COLOR_FORMAT,
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
      },
      {
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_update_callback(ren_rg_update_callback(OpaquePassData) {
    RgTextureSizeInfo size = {
        .width = data.size.x,
        .height = data.size.y,
        .num_array_layers = 1,
        .num_mip_levels = 1,
    };
    rg.resize_texture(texture, size);
    rg.resize_texture(depth_texture, size);
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(OpaquePassData) {
    run_opaque_pass(device, rg, render_pass, rcs, data);
  });
}

} // namespace ren
