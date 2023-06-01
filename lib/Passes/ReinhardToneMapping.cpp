#include "Passes/ReinhardToneMapping.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "PipelineLoading.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/postprocess_interface.hpp"

namespace ren {

namespace {

struct ReinhardToneMappingPassResources {
  RGTextureID texture;
  TextureIDAllocator *texture_allocator;
  Handle<ComputePipeline> pipeline;
};

void run_reinhard_tone_mapping_pass(
    Device &device, RenderGraph &rg, CommandBuffer &cmd,
    const ReinhardToneMappingPassResources &rcs) {
  assert(rcs.texture);
  assert(rcs.texture_allocator);
  assert(rcs.pipeline);

  auto texture = rg.get_texture(rcs.texture);
  auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
  auto index = rcs.texture_allocator->allocate_frame_storage_texture(texture);

  cmd.bind_compute_pipeline(rcs.pipeline);

  std::array sets = {rcs.texture_allocator->get_set()};
  cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, sets);

  glsl::ReinhardConstants pcs = {
      .tex = index,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, pcs);

  glm::uvec2 size = device.get_texture_view_size(texture);
  glm::uvec2 group_size = {glsl::REINHARD_THREADS_X, glsl::REINHARD_THREADS_Y};
  cmd.dispatch_threads(size, group_size);
}

} // namespace

auto setup_reinhard_tone_mapping_pass(Device &device, RenderGraph::Builder &rgb,
                                      const ReinhardToneMappingPassConfig &cfg)
    -> ToneMappingPassOutput {
  assert(cfg.texture);
  assert(cfg.texture_allocator);
  assert(cfg.pipelines);

  auto pass = rgb.create_pass("Reinhard tone mapping");

  auto texture = pass.write_texture({
      .name = "Color buffer after Reinhard tone mapping",
      .texture = cfg.texture,
      .state =
          {
              .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
              .accesses = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
              .layout = VK_IMAGE_LAYOUT_GENERAL,
          },
  });

  ReinhardToneMappingPassResources rcs = {
      .texture = texture,
      .texture_allocator = cfg.texture_allocator,
      .pipeline = cfg.pipelines->reinhard_tone_mapping,
  };

  pass.set_callback([rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    run_reinhard_tone_mapping_pass(device, rg, cmd, rcs);
  });

  return {
      .texture = texture,
  };
}

} // namespace ren
