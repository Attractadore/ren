#include "PostprocessPasses.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "Support/Variant.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/interface.hpp"
#include "glsl/postprocess_interface.hpp"

namespace ren {

namespace {

struct ExposurePassConfig {
  RGTextureID rt;
  PostprocessingOptions::Camera camera;
  PostprocessingOptions::Exposure options;
  TextureIDAllocator *texture_allocator;
  Handle<ComputePipeline> build_luminance_histogram_pipeline;
  Handle<ComputePipeline> reduce_luminance_histogram_pipeline;
};

struct ExposurePassOutput {
  RGBufferID exposure_buffer;
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
  auto exposure_buffer = pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Exposure buffer"),
          .size = sizeof(float),
      },
      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE);

  pass.set_callback([exposure_buffer, exposure](Device &device, RenderGraph &rg,
                                                CommandBuffer &cmd) {
    *device.map_buffer<float>(rg.get_buffer(exposure_buffer)) = exposure;
  });

  return {
      .exposure_buffer = exposure_buffer,
  };
};

auto setup_automatic_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                   const ExposurePassConfig &cfg)
    -> ExposurePassOutput {
  auto init_pass = rgb.create_pass();

  auto histogram_buffer = init_pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Luminance histogram"),
          .size = sizeof(glsl::LuminanceHistogram),
      },
      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT);

  init_pass.set_callback(
      [histogram_buffer](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        cmd.fill_buffer(rg.get_buffer(histogram_buffer), 0);
      });

  auto build_pass = rgb.create_pass();

  build_pass.read_texture(cfg.rt, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_IMAGE_LAYOUT_GENERAL);

  histogram_buffer =
      build_pass.write_buffer(histogram_buffer,
                              VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

  struct BuildLumianceHistogramPassResources {
    RGTextureID rt;
    RGBufferID histogram_buffer;
    TextureIDAllocator *texture_allocator;
    Handle<ComputePipeline> pipeline;
  } build_rcs = {
      .rt = cfg.rt,
      .histogram_buffer = histogram_buffer,
      .texture_allocator = cfg.texture_allocator,
      .pipeline = cfg.build_luminance_histogram_pipeline,
  };

  build_pass.set_callback([rcs = build_rcs](Device &device, RenderGraph &rg,
                                            CommandBuffer &cmd) {
    auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
    auto buffer = rg.get_buffer(rcs.histogram_buffer);
    auto texture = rg.get_texture(rcs.rt);
    auto texture_index =
        rcs.texture_allocator->allocate_frame_storage_texture(texture);

    cmd.bind_compute_pipeline(rcs.pipeline);

    std::array sets = {rcs.texture_allocator->get_set()};
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, sets);

    glsl::BuildLuminanceHistogramConstants constants = {
        .histogram_ptr = device.get_buffer_device_address(buffer),
        .tex = texture_index,
    };
    cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, constants);

    auto size = device.get_texture_view_size(texture);
    glm::uvec2 group_size = {glsl::BUILD_LUMINANCE_HISTOGRAM_THREADS_X,
                             glsl::BUILD_LUMINANCE_HISTOGRAM_THREADS_Y};
    cmd.dispatch_threads(size, group_size);
  });

  auto reduce_pass = rgb.create_pass();

  reduce_pass.read_buffer(histogram_buffer, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

  auto exposure_buffer = reduce_pass.create_buffer(
      {
          REN_SET_DEBUG_NAME("Automatic exposure buffer"),
          .size = sizeof(glsl::Exposure),
      },
      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

  struct ReduceLuminanceHistogramPassResources {
    RGTextureID rt;
    RGBufferID histogram_buffer;
    RGBufferID exposure_buffer;
    Handle<ComputePipeline> pipeline;
    float exposure_correction;
  } reduce_rcs = {
      .rt = cfg.rt,
      .histogram_buffer = histogram_buffer,
      .exposure_buffer = exposure_buffer,
      .pipeline = cfg.reduce_luminance_histogram_pipeline,
      .exposure_correction = cfg.options.compensation,
  };

  reduce_pass.set_callback(
      [rcs = reduce_rcs](Device &device, RenderGraph &rg, CommandBuffer &cmd) {
        auto layout = device.get_compute_pipeline(rcs.pipeline).layout;
        auto histogram_buffer = rg.get_buffer(rcs.histogram_buffer);
        auto exposure_buffer = rg.get_buffer(rcs.exposure_buffer);
        auto texture = rg.get_texture(rcs.rt);

        cmd.bind_compute_pipeline(rcs.pipeline);

        auto size = device.get_texture_view_size(texture);
        auto half_sum = size.x * size.y / 2;
        glsl::ReduceLuminanceHistogramConstants constants = {
            .histogram_ptr = device.get_buffer_device_address(histogram_buffer),
            .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
            .histogram_half_sum = half_sum,
            .exposure_correction = rcs.exposure_correction,
        };
        cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, constants);

        cmd.dispatch_groups(1);
      });

  return {
      .exposure_buffer = exposure_buffer,
  };
}

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
            return setup_automatic_exposure_pass(device, rgb, cfg);
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

  glsl::ReinhardPushConstants pcs = {
      .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
      .tex = index,
  };
  cmd.set_push_constants(layout, VK_SHADER_STAGE_COMPUTE_BIT, pcs);

  glm::uvec2 size = device.get_texture_view_size(texture);
  glm::uvec2 group_size = {glsl::REINHARD_THREADS_X, glsl::REINHARD_THREADS_Y};
  cmd.dispatch_threads(size, group_size);
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

  auto exposure = setup_exposure_pass(
      device, rgb,
      ExposurePassConfig{
          .rt = texture,
          .camera = cfg.options.camera,
          .options = cfg.options.exposure,
          .texture_allocator = cfg.texture_allocator,
          .build_luminance_histogram_pipeline =
              cfg.pipelines.build_luminance_histogram_pipeline,
          .reduce_luminance_histogram_pipeline =
              cfg.pipelines.reduce_luminance_histogram_pipeline,
      });

  auto tonemap = setup_tonemap_pass(
      device, rgb,
      TonemapPassConfig{
          .texture = texture,
          .exposure_buffer = exposure.exposure_buffer,
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
