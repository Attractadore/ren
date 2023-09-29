#include "Passes/EarlyZ.hpp"
#include "CommandRecorder.hpp"
#include "Mesh.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.hpp"

namespace ren {

namespace {

struct EarlyZPassResources {
  Handle<GraphicsPipeline> pipeline;
  RgBufferId transform_matrices;
};

void run_early_z_pass(const RgRuntime &rg, RenderPass &render_pass,
                      const EarlyZPassResources &rcs,
                      const EarlyZPassData &data) {
  const BufferView &transform_matrices = rg.get_buffer(rcs.transform_matrices);
  render_pass.bind_graphics_pipeline(rcs.pipeline);
  render_pass.bind_index_buffer(data.vertex_indices, VK_INDEX_TYPE_UINT32);
  render_pass.set_push_constants(glsl::EarlyZConstants{
      .positions = g_renderer->get_buffer_device_address<glsl::Positions>(
          data.vertex_positions),
      .transform_matrices =
          g_renderer->get_buffer_device_address<glsl::TransformMatrices>(
              transform_matrices),
      .pv = data.proj * data.view,
  });

  for (const auto &&[index, mesh_instance] : data.mesh_instances | enumerate) {
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

void setup_early_z_pass(RgBuilder &rgb, const EarlyZPassConfig &cfg) {
  EarlyZPassResources rcs = {};
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass("early-z");

  rcs.transform_matrices =
      pass.read_buffer("transform-matrices", RG_VS_READ_BUFFER);

  glm::uvec2 viewport_size = cfg.viewport_size;

  pass.create_depth_attachment(
      {
          .name = "depth-buffer-after-early-z",
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
    return viewport_size == data.viewport_size;
  });

  pass.set_graphics_callback(ren_rg_graphics_callback(EarlyZPassData) {
    run_early_z_pass(rg, render_pass, rcs, data);
  });
}

} // namespace ren
