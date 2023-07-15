#include "Passes/Opaque.hpp"
#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "Support/Views.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/OpaquePass.hpp"

namespace ren {

namespace {

struct OpaquePassResources {
  const HandleMap<Mesh> *meshes = nullptr;
  std::span<const MeshInst> mesh_insts;
  RGBufferID uniform_buffer;
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  RGBufferID directional_lights_buffer;
  RGBufferID materials_buffer;
  RGBufferID exposure_buffer;
  Handle<GraphicsPipeline> pipeline;
  TextureIDAllocator *texture_allocator = nullptr;
  glm::uvec2 size;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
  u32 num_dir_lights = 0;
};

void run_opaque_pass(Device &device, RGRuntime &rg, RenderPass &render_pass,
                     const OpaquePassResources &rcs) {
  assert(glm::all(glm::greaterThan(rcs.size, glm::uvec2(0))));

  auto size = rcs.size;
  render_pass.set_viewports(
      {{.width = float(size.x), .height = float(size.y), .maxDepth = 1.0f}});
  render_pass.set_scissor_rects({{.extent = {size.x, size.y}}});

  if (rcs.mesh_insts.empty()) {
    return;
  }

  assert(rcs.meshes);
  assert(rcs.texture_allocator);

  const auto &transform_matrix_buffer =
      rg.get_buffer(rcs.transform_matrix_buffer);
  const auto &normal_matrix_buffer = rg.get_buffer(rcs.normal_matrix_buffer);
  Optional<const BufferView &> directional_lights_buffer;
  if (rcs.directional_lights_buffer) {
    directional_lights_buffer = rg.get_buffer(rcs.directional_lights_buffer);
  } else {
    assert(rcs.num_dir_lights == 0);
  }
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
      .directional_lights = directional_lights_buffer.map_or(
          [&](const BufferView &view) {
            return device.get_buffer_device_address<glsl::DirectionalLights>(
                view);
          },
          BufferReference<glsl::DirectionalLights>()),
      .exposure =
          device.get_buffer_device_address<glsl::Exposure>(exposure_buffer),
      .pv = rcs.proj * rcs.view,
      .eye = rcs.eye,
      .num_directional_lights = rcs.num_dir_lights,
  };

  render_pass.bind_graphics_pipeline(rcs.pipeline);

  render_pass.bind_descriptor_sets(rcs.texture_allocator->get_set());

  auto ub = device.get_buffer_device_address<glsl::OpaqueUniformBuffer>(
      uniform_buffer);
  for (const auto &&[i, mesh_inst] : enumerate(rcs.mesh_insts)) {
    const auto &mesh = (*rcs.meshes)[mesh_inst.mesh];
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
  assert(cfg.meshes);
  assert(cfg.pipeline);
  assert(cfg.texture_allocator);
  assert(cfg.exposure_buffer);
  assert(glm::all(glm::greaterThan(cfg.size, glm::uvec2(0))));

  auto pass = rgb.create_pass({
      .name = "Opaque",
      .type = RGPassType::Graphics,
  });

  if (cfg.transform_matrix_buffer) {
    assert(cfg.normal_matrix_buffer);
    pass.read_vertex_shader_buffer({.buffer = cfg.transform_matrix_buffer});
    pass.read_vertex_shader_buffer({.buffer = cfg.normal_matrix_buffer});
  }

  if (cfg.directional_lights_buffer) {
    pass.read_fragment_shader_buffer({.buffer = cfg.directional_lights_buffer});
  };

  if (cfg.materials_buffer) {
    pass.read_fragment_shader_buffer({.buffer = cfg.materials_buffer});
  }

  pass.read_fragment_shader_buffer({.buffer = cfg.exposure_buffer});

  auto uniform_buffer = pass.create_uniform_buffer({
      .name = "Opaque pass uniforms",
      .size = sizeof(glsl::OpaqueUniformBuffer),
  });

  auto texture = pass.create_color_attachment({
      .name = "Color buffer after opaque pass",
      .format = COLOR_FORMAT,
      .size = {cfg.size, 1},
  });

  auto depth_texture = pass.create_depth_attachment({
      .name = "Depth buffer after opaque pass",
      .format = DEPTH_FORMAT,
      .size = {cfg.size, 1},
  });

  OpaquePassResources rcs = {
      .meshes = cfg.meshes,
      .mesh_insts = cfg.mesh_insts,
      .uniform_buffer = uniform_buffer,
      .transform_matrix_buffer = cfg.transform_matrix_buffer,
      .normal_matrix_buffer = cfg.normal_matrix_buffer,
      .directional_lights_buffer = cfg.directional_lights_buffer,
      .materials_buffer = cfg.materials_buffer,
      .exposure_buffer = cfg.exposure_buffer,
      .pipeline = cfg.pipeline,
      .texture_allocator = cfg.texture_allocator,
      .size = cfg.size,
      .proj = cfg.proj,
      .view = cfg.view,
      .eye = cfg.eye,
      .num_dir_lights = cfg.num_dir_lights,
  };

  pass.set_graphics_callback(
      [rcs](Device &device, RGRuntime &rg, RenderPass &render_pass) {
        run_opaque_pass(device, rg, render_pass, rcs);
      });

  return {
      .texture = texture,
  };
}

} // namespace ren
