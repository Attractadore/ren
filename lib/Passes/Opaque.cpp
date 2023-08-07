#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "Support/Views.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/OpaquePass.hpp"

namespace ren {

namespace {

struct OpaquePassResources {
  Handle<GraphicsPipeline> pipeline;
  RgRtBuffer uniform_buffer;
  RgRtBuffer transform_matrices;
  RgRtBuffer normal_matrices;
  RgRtBuffer directional_lights_buffer;
  RgRtBuffer materials_buffer;
  RgRtBuffer exposure_buffer;
};

void run_opaque_pass(Device &device, const RgRuntime &rg,
                     RenderPass &render_pass, const OpaquePassResources &rcs,
                     const OpaquePassData &data) {
  assert(glm::all(glm::greaterThan(data.size, glm::uvec2(0))));
  assert(data.meshes);

  auto size = data.size;
  render_pass.set_viewports(
      {{.width = float(size.x), .height = float(size.y), .maxDepth = 1.0f}});
  render_pass.set_scissor_rects({{.extent = {size.x, size.y}}});

  if (data.mesh_insts.empty()) {
    return;
  }

  const auto &transform_matrix_buffer = rg.get_buffer(rcs.transform_matrices);
  const auto &normal_matrix_buffer = rg.get_buffer(rcs.normal_matrices);
  const auto &directional_lights_buffer =
      rg.get_buffer(rcs.directional_lights_buffer);
  const auto &materials_buffer = rg.get_buffer(rcs.materials_buffer);
  const auto &exposure_buffer = rg.get_buffer(rcs.exposure_buffer);

  const auto &uniform_buffer = rg.get_buffer(rcs.uniform_buffer);
  auto *uniforms = device.map_buffer<glsl::OpaqueUniformBuffer>(uniform_buffer);
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
      uniform_buffer);
  for (const auto &&[i, mesh_inst] : enumerate(data.mesh_insts)) {
    const auto &mesh = (*data.meshes)[mesh_inst.mesh];
    auto material = mesh_inst.material;

    auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
    auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
    auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
    auto uvs_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_UVS];

    render_pass.set_push_constants(glsl::OpaqueConstants{
        .ub = ub,
        .positions = device.get_buffer_device_address<glsl::Positions>(
            mesh.vertex_buffer, positions_offset),
        .colors = (colors_offset != ATTRIBUTE_UNUSED)
                      ? device.get_buffer_device_address<glsl::Colors>(
                            mesh.vertex_buffer, colors_offset)
                      : nullptr,
        .normals = device.get_buffer_device_address<glsl::Normals>(
            mesh.vertex_buffer, normals_offset),
        .uvs = (uvs_offset != ATTRIBUTE_UNUSED)
                   ? device.get_buffer_device_address<glsl::UVs>(
                         mesh.vertex_buffer, uvs_offset)
                   : nullptr,
        .matrix = unsigned(i),
        .material = material,
    });

    render_pass.bind_index_buffer(mesh.index_buffer, mesh.index_format);
    render_pass.draw_indexed({
        .num_indices = mesh.num_indices,
    });
  }
}

} // namespace

auto setup_opaque_pass(Device &device, RenderGraph::Builder &rgb,
                       const OpaquePassConfig &cfg) -> OpaquePassOutput {
  assert(cfg.pipeline);

  OpaquePassResources rcs;
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass({
      .name = "Opaque",
      .type = RgPassType::Graphics,
  });

  rcs.transform_matrices =
      pass.read_buffer(cfg.transform_matrices, RG_VS_READ_BUFFER);
  rcs.normal_matrices =
      pass.read_buffer(cfg.normal_matrices, RG_VS_READ_BUFFER);

  rcs.directional_lights_buffer =
      pass.read_buffer(cfg.directional_lights, RG_FS_READ_BUFFER);

  rcs.materials_buffer = pass.read_buffer(cfg.materials, RG_FS_READ_BUFFER);

  rcs.exposure_buffer = pass.read_buffer(cfg.exposure, RG_FS_READ_BUFFER,
                                         cfg.exposure_temporal_offset);

  RgBuffer uniform_buffer;
  std::tie(uniform_buffer, rcs.uniform_buffer) = pass.create_buffer({
      .name = "Opaque pass uniforms",
      .size = sizeof(glsl::OpaqueUniformBuffer),
      .usage = RG_VS_READ_BUFFER | RG_FS_READ_BUFFER,
  });

  auto texture = pass.create_color_attachment(
      {
          .name = "Color buffer after opaque pass",
          .format = COLOR_FORMAT,
      },
      ColorAttachmentOperations{
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
      });

  auto depth_texture = pass.create_depth_attachment(
      {
          .name = "Depth buffer after opaque pass",
          .format = DEPTH_FORMAT,
      },
      DepthAttachmentOperations{
          .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .store = VK_ATTACHMENT_STORE_OP_STORE,
          .clear_depth = 0.0f,
      });

  pass.set_size_callback(ren_rg_size_callback(OpaquePassData) {
    rg.resize_texture(texture, {data.size, 1});
    rg.resize_texture(depth_texture, {data.size, 1});
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(OpaquePassData) {
    run_opaque_pass(device, rg, render_pass, rcs, data);
  });

  return {
      .pass = pass,
      .texture = texture,
  };
}

} // namespace ren
