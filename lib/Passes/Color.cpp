#include "Passes/Color.hpp"
#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "Support/Views.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/color_interface.hpp"

namespace ren {

namespace {

struct ColorPassResources {
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

void run_color_pass(Device &device, RGRuntime &rg, RenderPass &render_pass,
                    const ColorPassResources &rcs) {
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
  auto *uniforms = device.map_buffer<glsl::ColorUB>(uniform_buffer);
  *uniforms = {
      .transform_matrices_ptr =
          device.get_buffer_device_address(transform_matrix_buffer),
      .normal_matrices_ptr =
          device.get_buffer_device_address(normal_matrix_buffer),
      .materials_ptr = device.get_buffer_device_address(materials_buffer),
      .directional_lights_ptr = directional_lights_buffer.map_or(
          [&](const BufferView &view) {
            return device.get_buffer_device_address(view);
          },
          u64(0)),
      .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
      .proj_view = rcs.proj * rcs.view,
      .eye = rcs.eye,
      .num_dir_lights = rcs.num_dir_lights,
  };

  render_pass.bind_graphics_pipeline(rcs.pipeline);

  render_pass.bind_descriptor_sets(rcs.texture_allocator->get_set());

  auto ub_ptr = device.get_buffer_device_address(uniform_buffer);
  for (const auto &&[i, mesh_inst] : enumerate(rcs.mesh_insts)) {
    const auto &mesh = (*rcs.meshes)[mesh_inst.mesh];
    auto material = mesh_inst.material;

    auto address = device.get_buffer_device_address(mesh.vertex_buffer);
    auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
    auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
    auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
    auto uvs_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_UVS];

    render_pass.set_push_constants(glsl::ColorConstants{
        .ub_ptr = ub_ptr,
        .positions_ptr = address + positions_offset,
        .colors_ptr =
            (colors_offset != ATTRIBUTE_UNUSED) ? address + colors_offset : 0,
        .normals_ptr = address + normals_offset,
        .uvs_ptr = (uvs_offset != ATTRIBUTE_UNUSED) ? address + uvs_offset : 0,
        .matrix_index = unsigned(i),
        .material_index = material,
    });

    render_pass.bind_index_buffer(mesh.index_buffer, mesh.index_format);
    render_pass.draw_indexed({
        .num_indices = mesh.num_indices,
    });
  }
}

} // namespace

auto setup_color_pass(Device &device, RenderGraph::Builder &rgb,
                      const ColorPassConfig &cfg) -> ColorPassOutput {
  assert(cfg.meshes);
  assert(cfg.pipeline);
  assert(cfg.texture_allocator);
  assert(cfg.exposure_buffer);
  assert(glm::all(glm::greaterThan(cfg.size, glm::uvec2(0))));

  auto pass = rgb.create_pass({
      .name = "Color",
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
      .name = "Color pass uniforms",
      .size = sizeof(glsl::ColorUB),
  });

  auto texture = pass.create_color_attachment({
      .name = "Color buffer after color pass",
      .format = COLOR_FORMAT,
      .size = {cfg.size, 1},
  });

  auto depth_texture = pass.create_depth_attachment({
      .name = "Depth buffer after color pass",
      .format = DEPTH_FORMAT,
      .size = {cfg.size, 1},
  });

  ColorPassResources rcs = {
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
        run_color_pass(device, rg, render_pass, rcs);
      });

  return {
      .texture = texture,
  };
}

} // namespace ren
