#include "Passes/Color.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Support/Views.hpp"
#include "glsl/color_interface.hpp"

namespace ren {

namespace {

struct ColorPassResources {
  const HandleMap<Mesh> *meshes = nullptr;
  std::span<const MeshInst> mesh_insts;
  std::span<const Handle<GraphicsPipeline>> material_pipelines;
  RGTextureID texture;
  RGTextureID depth_texture;
  RGBufferID uniform_buffer;
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  RGBufferID directional_lights_buffer;
  RGBufferID materials_buffer;
  RGBufferID exposure_buffer;
  Handle<PipelineLayout> pipeline_layout;
  VkDescriptorSet persistent_set = nullptr;
  glm::mat4 proj;
  glm::mat4 view;
  glm::vec3 eye;
  u32 num_dir_lights = 0;
};

void run_color_pass(Device &device, RenderGraph &rg, CommandBuffer &cmd,
                    const ColorPassResources &rcs) {
  assert(rcs.texture);
  assert(rcs.depth_texture);

  const auto &texture = rg.get_texture(rcs.texture);
  cmd.begin_rendering(rg.get_texture(rcs.texture),
                      rg.get_texture(rcs.depth_texture));

  auto size = device.get_texture_view_size(texture);
  cmd.set_viewport(
      {.width = float(size.x), .height = float(size.y), .maxDepth = 1.0f});
  cmd.set_scissor_rect({.extent = {size.x, size.y}});

  if (not rcs.mesh_insts.empty()) {
    assert(rcs.meshes);
    assert(not rcs.material_pipelines.empty());
    assert(rcs.persistent_set);
    assert(rcs.uniform_buffer);
    assert(rcs.transform_matrix_buffer);
    assert(rcs.normal_matrix_buffer);
    assert(rcs.materials_buffer);
    assert(rcs.exposure_buffer);
    assert(rcs.pipeline_layout);

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

    std::array descriptor_sets = {rcs.persistent_set};
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                             rcs.pipeline_layout, 0, descriptor_sets);

    auto ub_ptr = device.get_buffer_device_address(uniform_buffer);
    for (const auto &&[i, mesh_inst] : enumerate(rcs.mesh_insts)) {
      const auto &mesh = (*rcs.meshes)[mesh_inst.mesh];
      auto material = mesh_inst.material;

      cmd.bind_graphics_pipeline(rcs.material_pipelines[material]);

      auto address = device.get_buffer_device_address(mesh.vertex_buffer);
      auto positions_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_POSITIONS];
      auto normals_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_NORMALS];
      auto colors_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_COLORS];
      auto uvs_offset = mesh.attribute_offsets[MESH_ATTRIBUTE_UVS];
      glsl::ColorConstants pcs = {
          .ub_ptr = ub_ptr,
          .positions_ptr = address + positions_offset,
          .colors_ptr =
              (colors_offset != ATTRIBUTE_UNUSED) ? address + colors_offset : 0,
          .normals_ptr = address + normals_offset,
          .uvs_ptr =
              (uvs_offset != ATTRIBUTE_UNUSED) ? address + uvs_offset : 0,
          .matrix_index = unsigned(i),
          .material_index = material,
      };
      cmd.set_push_constants(
          rcs.pipeline_layout,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pcs);

      cmd.bind_index_buffer(mesh.index_buffer, mesh.index_format);
      cmd.draw_indexed({
          .num_indices = mesh.num_indices,
      });
    }
  }

  cmd.end_rendering();
}

} // namespace

auto setup_color_pass(Device &device, RenderGraph::Builder &rgb,
                      const ColorPassConfig &cfg) -> ColorPassOutput {
  assert(cfg.meshes);
  assert(cfg.persistent_set);
  assert(cfg.pipeline_layout);
  assert(cfg.exposure_buffer);
  assert(cfg.persistent_set);
  assert(cfg.color_format);
  assert(cfg.depth_format);
  assert(glm::all(glm::greaterThan(cfg.size, glm::uvec2(0))));

  auto pass = rgb.create_pass("Color");

  for (auto buffer : cfg.uploaded_vertex_buffers) {
    pass.read_buffer({
        .buffer = buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            },
    });
  }

  for (auto buffer : cfg.uploaded_index_buffers) {
    pass.read_buffer({
        .buffer = buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                .accesses = VK_ACCESS_2_INDEX_READ_BIT,
            },
    });
  }

  for (auto texture : cfg.uploaded_textures) {
    pass.read_texture({
        .texture = texture,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
    });
  }

  if (cfg.transform_matrix_buffer) {
    assert(cfg.normal_matrix_buffer);
    pass.read_buffer({
        .buffer = cfg.transform_matrix_buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            },
    });
    pass.read_buffer({
        .buffer = cfg.normal_matrix_buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            },
    });
  }

  if (cfg.directional_lights_buffer) {
    pass.read_buffer({
        .buffer = cfg.directional_lights_buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            },
    });
  };

  if (cfg.materials_buffer) {
    pass.read_buffer({
        .buffer = cfg.materials_buffer,
        .state =
            {
                .stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            },
    });
  }

  pass.read_buffer({
      .buffer = cfg.exposure_buffer,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          },
  });

  auto uniform_buffer = pass.create_buffer({
      .name = "Color pass uniforms",
      REN_SET_DEBUG_NAME("Color pass uniform buffer"),
      .heap = BufferHeap::Upload,
      .size = sizeof(glsl::ColorUB),
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          },
  });

  auto texture = pass.create_texture({
      .name = "Color buffer after color pass",
      REN_SET_DEBUG_NAME("Color buffer"),
      .format = cfg.color_format,
      .size = {cfg.size, 1},
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              .accesses = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
          },
  });

  auto depth_texture = pass.create_texture({
      .name = "Depth buffer after color pass",
      REN_SET_DEBUG_NAME("Depth buffer"),
      .format = cfg.depth_format,
      .size = {cfg.size, 1},
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
              .accesses = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,

          },
  });

  ColorPassResources rcs = {
      .meshes = cfg.meshes,
      .mesh_insts = cfg.mesh_insts,
      .material_pipelines = cfg.material_pipelines,
      .texture = texture,
      .depth_texture = depth_texture,
      .uniform_buffer = uniform_buffer,
      .transform_matrix_buffer = cfg.transform_matrix_buffer,
      .normal_matrix_buffer = cfg.normal_matrix_buffer,
      .directional_lights_buffer = cfg.directional_lights_buffer,
      .materials_buffer = cfg.materials_buffer,
      .exposure_buffer = cfg.exposure_buffer,
      .pipeline_layout = cfg.pipeline_layout,
      .persistent_set = cfg.persistent_set,
      .proj = cfg.proj,
      .view = cfg.view,
      .eye = cfg.eye,
      .num_dir_lights = cfg.num_dir_lights,
  };

  pass.set_callback([rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    run_color_pass(device, rg, cmd, rcs);
  });

  return {
      .texture = texture,
  };
}

} // namespace ren
