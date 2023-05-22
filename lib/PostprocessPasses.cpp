#include "PostprocessPasses.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/Variant.hpp"
#include "TextureIDAllocator.hpp"
#include "hlsl/interface.hpp"
#include "hlsl/postprocess_interface.hpp"

namespace ren {

namespace {

struct ExposurePassConfig {
  RGTextureID rt;
  PostprocessingOptions::Camera camera;
  PostprocessingOptions::Exposure options;
};

struct ExposurePassOutput {
  RGBufferID buffer;
};

auto get_camera_exposure(const PostprocessingOptions::Camera &camera,
                         float ec = 0.0f) -> float {
  auto ev100_pow2 = camera.aperture * camera.aperture / camera.shutter_time *
                    100.0f / camera.iso;
  auto max_luminance = 1.2f * ev100_pow2 * glm::exp2(-ec);
  return 1.0f / max_luminance;
};

auto setup_manual_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                const ExposurePassConfig &cfg, float exposure)
    -> ExposurePassOutput {
  auto pass = rgb.create_pass();
  auto buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Exposure buffer"),
          .size = sizeof(float),
      },
      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  pass.set_callback(
      [buffer, exposure](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        *device.map_buffer<float>(rg.get_buffer(buffer)) = exposure;
      });

  return ExposurePassOutput{
      .buffer = buffer,
  };
};

auto setup_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                         const ExposurePassConfig &cfg) -> ExposurePassOutput {
  return std::visit(
      OverloadSet{
          [&](const PostprocessingOptions::Exposure::Manual &manual) {
            return setup_manual_exposure_pass(device, rgb, cfg,
                                              manual.exposure);
          },
          [&](const PostprocessingOptions::Exposure::Camera &camera) {
            return setup_manual_exposure_pass(
                device, rgb, cfg,
                get_camera_exposure(cfg.camera, cfg.options.compensation));
          },
          [&](const PostprocessingOptions::Exposure::Automatic &automatic)
              -> ExposurePassOutput {
            todo("Automatic exposure not implemented");
          },
      },
      cfg.options.mode);
}

struct TonemapPassConfig {
  RGTextureID texture;
  RGBufferID exposure_buffer;
  PostprocessingOptions::Tonemapping options;
  Handle<ComputePipeline> reinhard_tonemap_pipeline;
  TextureIDAllocator *texture_allocator;
};

struct TonemapPassOutput {
  RGTextureID texture;
};

struct ReinhardTonemapPassResources {
  RGTextureID texture;
  RGBufferID exposure_buffer;
  Handle<ComputePipeline> pipeline;
  TextureIDAllocator *texture_allocator;
};

void run_reinhard_tonemap_pass(Device &device, RenderGraph &rg,
                               CommandBuffer &cmd,
                               const ReinhardTonemapPassResources &rcs) {
  auto texture = rg.get_texture(rcs.texture);
  auto exposure_buffer = rg.get_buffer(rcs.exposure_buffer);
  auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
  auto index = rcs.texture_allocator->allocate_frame_storage_texture(texture);

  cmd.bind_compute_pipeline(rcs.pipeline);

  std::array sets = {rcs.texture_allocator->get_set()};
  cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, sets);

  hlsl::ReinhardPushConstants pcs = {
      .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
      .tex = index,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, pcs);

  glm::uvec2 size = device.get_texture_view_size(texture);
  glm::uvec2 dims = {hlsl::REINHARD_THREADS_X, hlsl::REINHARD_THREADS_Y};
  auto num_groups =
      size / dims + glm::uvec2(glm::greaterThan(size % dims, glm::uvec2(0)));
  cmd.dispatch(num_groups);
}

auto setup_tonemap_pass(Device &device, RenderGraph::Builder &rgb,
                        const TonemapPassConfig &cfg) -> TonemapPassOutput {
  auto pass = rgb.create_pass();
  pass.read_buffer(cfg.exposure_buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
  auto rt = pass.write_texture(
      cfg.texture,
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);

  switch (cfg.options.oper) {
  case REN_TONEMAPPING_OPERATOR_REINHARD: {
    ReinhardTonemapPassResources rcs = {
        .texture = rt,
        .exposure_buffer = cfg.exposure_buffer,
        .pipeline = cfg.reinhard_tonemap_pipeline,
        .texture_allocator = cfg.texture_allocator,
    };
    pass.set_callback(
        [rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
          run_reinhard_tonemap_pass(device, rg, cmd, rcs);
        });
  } break;
  case REN_TONEMAPPING_OPERATOR_ACES: {
    todo("ACES tonemapping not implemented");
  } break;
  }

  return {
      .texture = rt,
  };
}

} // namespace

auto setup_postprocess_passes(Device &device, RenderGraph::Builder &rgb,
                              const PostprocessPassesConfig &cfg)
    -> PostprocessPassesOutput {
  auto texture = cfg.texture;

  auto exposure = setup_exposure_pass(device, rgb,
                                      ExposurePassConfig{
                                          .rt = texture,
                                          .camera = cfg.options.camera,
                                          .options = cfg.options.exposure,
                                      });

  auto tonemap = setup_tonemap_pass(
      device, rgb,
      TonemapPassConfig{
          .texture = texture,
          .exposure_buffer = exposure.buffer,
          .options = cfg.options.tonemapping,
          .reinhard_tonemap_pipeline = cfg.pipelines.reinhard_tonemap,
          .texture_allocator = cfg.texture_allocator,
      });
  texture = tonemap.texture;

  return {
      .texture = texture,
  };
}

} // namespace ren
