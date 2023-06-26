#include "Passes/PostProcessing.hpp"
#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "Support/Errors.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/post_processing_pass.hpp"
#include "glsl/reduce_luminance_histogram_pass.hpp"

namespace ren {

namespace {

struct InitializeLuminanceHistogramPassConfig {};

struct InitializeLuminanceHistogramPassOutput {
  RGBufferID histogram_buffer;
};

auto setup_initialize_luminance_histogram_pass(
    Device &device, RenderGraph::Builder &rgb,
    const InitializeLuminanceHistogramPassConfig &cfg)
    -> InitializeLuminanceHistogramPassOutput {
  auto pass = rgb.create_pass({
      .name = "Automatic exposure: initialize luminance histogram",
      .type = RGPassType::Transfer,
  });

  auto histogram = pass.create_transfer_buffer({
      .name = "Empty luminance histogram",
      .size = sizeof(glsl::LuminanceHistogram),
  });

  pass.set_transfer_callback(
      [=](Device &device, RGRuntime &rg, CommandRecorder &cmd) {
        cmd.fill_buffer(rg.get_buffer(histogram), 0);
      });

  return {
      .histogram_buffer = histogram,
  };
};

struct PostProcessingUberPassResources {
  RGTextureID texture;
  RGBufferID histogram_buffer;
  RGBufferID previous_exposure_buffer;
  TextureIDAllocator *texture_allocator = nullptr;
  Handle<ComputePipeline> pipeline;
};

void run_post_processing_uber_pass(Device &device, RGRuntime &rg,
                                   ComputePass &pass,
                                   const PostProcessingUberPassResources &rcs) {
  assert(rcs.texture_allocator);

  const auto &texture = rg.get_texture(rcs.texture);
  auto texture_index =
      rcs.texture_allocator->allocate_frame_storage_texture(texture);

  pass.bind_compute_pipeline(rcs.pipeline);

  pass.bind_descriptor_set(rcs.texture_allocator->get_set());

  u64 histogram_ptr = 0;
  u64 previous_exposure_ptr = 0;
  if (rcs.histogram_buffer) {
    assert(rcs.previous_exposure_buffer);
    histogram_ptr =
        device.get_buffer_device_address(rg.get_buffer(rcs.histogram_buffer));
    previous_exposure_ptr = device.get_buffer_device_address(
        rg.get_buffer(rcs.previous_exposure_buffer));
  }
  pass.set_push_constants(glsl::PostProcessingConstants{
      .histogram_ptr = histogram_ptr,
      .previous_exposure_ptr = previous_exposure_ptr,
      .tex = texture_index,
  });

  auto size = device.get_texture_view_size(texture);
  glm::uvec2 group_size = {glsl::POST_PROCESSING_THREADS_X,
                           glsl::POST_PROCESSING_THREADS_Y};
  glm::uvec2 work_size = {glsl::POST_PROCESSING_WORK_SIZE_X,
                          glsl::POST_PROCESSING_WORK_SIZE_Y};
  pass.dispatch_threads(size, group_size * work_size);
}

struct PostProcessingUberPassConfig {
  RGTextureID texture;
  RGBufferID histogram_buffer;
  RGBufferID previous_exposure_buffer;
  TextureIDAllocator *texture_allocator = nullptr;
  Handle<ComputePipeline> pipeline;
};

struct PostProcessingUberPassOutput {
  RGTextureID texture;
  RGBufferID histogram_buffer;
};

auto setup_post_processing_uber_pass(Device &device, RenderGraph::Builder &rgb,
                                     const PostProcessingUberPassConfig &cfg)
    -> PostProcessingUberPassOutput {
  assert(cfg.texture);
  assert(cfg.texture_allocator);
  assert(cfg.pipeline);

  auto pass = rgb.create_pass({
      .name = "Post-processing",
      .type = RGPassType::Compute,
  });

  auto texture = pass.write_storage_texture({
      .name = "Color buffer after post-processing",
      .texture = cfg.texture,
  });

  RGBufferID histogram_buffer;
  if (cfg.histogram_buffer) {
    assert(cfg.previous_exposure_buffer);
    histogram_buffer = pass.write_compute_buffer({
        .name = "Luminance histogram",
        .buffer = cfg.histogram_buffer,
    });
    pass.read_compute_buffer({.buffer = cfg.previous_exposure_buffer});
  }

  PostProcessingUberPassResources rcs = {
      .texture = cfg.texture,
      .histogram_buffer = histogram_buffer,
      .previous_exposure_buffer = cfg.previous_exposure_buffer,
      .texture_allocator = cfg.texture_allocator,
      .pipeline = cfg.pipeline,
  };

  pass.set_compute_callback(
      [rcs](Device &device, RGRuntime &rg, ComputePass &pass) {
        run_post_processing_uber_pass(device, rg, pass, rcs);
      });

  return {
      .texture = texture,
      .histogram_buffer = histogram_buffer,
  };
}

struct ReduceLuminanceHistogramPassResources {
  RGBufferID histogram_buffer;
  RGBufferID exposure_buffer;
  Handle<ComputePipeline> pipeline;
  float exposure_compensation = 0.0f;
};

void run_reduce_luminance_histogram_pass(
    Device &device, RGRuntime &rg, ComputePass &pass,
    const ReduceLuminanceHistogramPassResources &rcs) {
  const auto &histogram_buffer = rg.get_buffer(rcs.histogram_buffer);
  const auto &exposure_buffer = rg.get_buffer(rcs.exposure_buffer);

  pass.bind_compute_pipeline(rcs.pipeline);

  pass.set_push_constants(glsl::ReduceLuminanceHistogramConstants{
      .histogram_ptr = device.get_buffer_device_address(histogram_buffer),
      .exposure_ptr = device.get_buffer_device_address(exposure_buffer),
      .exposure_compensation = rcs.exposure_compensation,
  });

  pass.dispatch_groups(1);
}

struct ReduceLuminanceHistogramPassConfig {
  RGBufferID histogram_buffer;
  Handle<ComputePipeline> pipeline;
  float exposure_compensation = 0.0f;
};

struct ReduceLuminanceHistogramPassOutput {
  RGBufferID exposure_buffer;
};

auto setup_reduce_luminance_histogram_pass(
    Device &device, RenderGraph::Builder &rgb,
    const ReduceLuminanceHistogramPassConfig &cfg)
    -> ReduceLuminanceHistogramPassOutput {
  assert(cfg.histogram_buffer);
  assert(cfg.pipeline);

  auto pass = rgb.create_pass({
      .name = "Automatic exposure: reduce luminance histogram",
      .type = RGPassType::Compute,
  });

  pass.read_compute_buffer({.buffer = cfg.histogram_buffer});

  auto exposure_buffer = pass.create_compute_buffer({
      .name = "Automatic exposure",
      .size = sizeof(glsl::Exposure),
      .temporal = true,
  });

  ReduceLuminanceHistogramPassResources rcs = {
      .histogram_buffer = cfg.histogram_buffer,
      .exposure_buffer = exposure_buffer,
      .pipeline = cfg.pipeline,
      .exposure_compensation = cfg.exposure_compensation,
  };

  pass.set_compute_callback(
      [rcs](Device &device, RGRuntime &rg, ComputePass &pass) {
        run_reduce_luminance_histogram_pass(device, rg, pass, rcs);
      });

  return {
      .exposure_buffer = exposure_buffer,
  };
}

} // namespace

auto setup_post_processing_passes(Device &device, RenderGraph::Builder &rgb,
                                  const PostProcessingPassesConfig &cfg)
    -> PostProcessingPassesOutput {
  assert(cfg.texture);
  assert(cfg.pipelines);
  assert(cfg.texture_allocator);
  assert(cfg.options);

  auto automatic_exposure =
      cfg.options->exposure.mode.get<ExposureOptions::Automatic>();

  RGBufferID histogram_buffer;
  if (automatic_exposure) {
    histogram_buffer =
        setup_initialize_luminance_histogram_pass(device, rgb, {})
            .histogram_buffer;
  }

  switch (cfg.options->tone_mapping.oper) {
  case REN_TONE_MAPPING_OPERATOR_REINHARD: {
  } break;
  case REN_TONE_MAPPING_OPERATOR_ACES: {
    todo("ACES tone mapping is not implemented!");
  } break;
  }

  auto [texture, histogram_buffer_after_build] =
      setup_post_processing_uber_pass(
          device, rgb,
          {
              .texture = cfg.texture,
              .histogram_buffer = histogram_buffer,
              .previous_exposure_buffer = cfg.previous_exposure_buffer,
              .texture_allocator = cfg.texture_allocator,
              .pipeline = cfg.pipelines->post_processing,
          });
  histogram_buffer = histogram_buffer_after_build;

  RGBufferID automatic_exposure_buffer;
  if (automatic_exposure) {
    automatic_exposure_buffer =
        setup_reduce_luminance_histogram_pass(
            device, rgb,
            {
                .histogram_buffer = histogram_buffer,
                .pipeline = cfg.pipelines->reduce_luminance_histogram,
                .exposure_compensation =
                    automatic_exposure->exposure_compensation,
            })
            .exposure_buffer;
  }

  return {
      .texture = texture,
      .exposure_buffer = automatic_exposure_buffer,
  };
}

} // namespace ren
