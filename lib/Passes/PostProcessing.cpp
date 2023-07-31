#include "Passes/PostProcessing.hpp"
#include "CommandRecorder.hpp"
#include "Device.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "Support/Errors.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/PostProcessingPass.hpp"
#include "glsl/ReduceLuminanceHistogramPass.hpp"

namespace ren {

namespace {

auto setup_initialize_luminance_histogram_pass(Device &device, RgBuilder &rgb)
    -> RgBuffer {
  auto pass = rgb.create_pass({
      .name = "Automatic exposure: initialize luminance histogram",
      .type = RgPassType::Transfer,
  });

  auto [histogram, rt_histogram] = pass.create_transfer_buffer({
      .name = "Empty luminance histogram",
      .size = sizeof(glsl::LuminanceHistogram),
  });

  pass.set_transfer_callback(ren_rg_transfer_callback(RgNoPassData) {
    cmd.fill_buffer(rg.get_buffer(rt_histogram), 0);
  });

  return histogram;
};

struct PostProcessingPassResources {
  Handle<ComputePipeline> pipeline;
  RgRtTexture texture;
  RgRtBuffer histogram;
  RgRtBuffer exposure;
};

void run_post_processing_uber_pass(Device &device, const RgRuntime &rg,
                                   ComputePass &pass,
                                   const PostProcessingPassResources &rcs) {
  const auto &[texture, texture_index] = rg.get_storage_texture(rcs.texture);

  pass.bind_compute_pipeline(rcs.pipeline);

  assert(!rcs.histogram == !rcs.exposure);
  pass.set_push_constants(glsl::PostProcessingConstants{
      .histogram =
          rcs.histogram
              ? device.get_buffer_device_address<glsl::LuminanceHistogram>(
                    rg.get_buffer(rcs.histogram))
              : nullptr,
      .previous_exposure =
          rcs.exposure ? device.get_buffer_device_address<glsl::Exposure>(
                             rg.get_buffer(rcs.exposure))
                       : nullptr,
      .tex = texture_index,
  });

  auto size = device.get_texture_view_size(texture);
  glm::uvec2 group_size = {glsl::POST_PROCESSING_THREADS_X,
                           glsl::POST_PROCESSING_THREADS_Y};
  glm::uvec2 work_size = {glsl::POST_PROCESSING_WORK_SIZE_X,
                          glsl::POST_PROCESSING_WORK_SIZE_Y};
  pass.dispatch_threads(size, group_size * work_size);
}

struct PostProcessingPassConfig {
  Handle<ComputePipeline> pipeline;
  RgTexture texture;
  RgBuffer histogram;
  RgBuffer exposure;
};

struct PostProcessingPassOutput {
  RgTexture texture;
  RgBuffer histogram;
};

auto setup_post_processing_uber_pass(Device &device, RenderGraph::Builder &rgb,
                                     const PostProcessingPassConfig &cfg)
    -> PostProcessingPassOutput {
  assert(cfg.pipeline);
  assert(cfg.texture);

  auto pass = rgb.create_pass({
      .name = "Post-processing",
      .type = RgPassType::Compute,
  });

  auto [texture, rt_texture] = pass.write_storage_texture({
      .name = "Color buffer after post-processing",
      .texture = cfg.texture,
  });

  RgBuffer histogram;
  RgRtBuffer rt_histogram;
  RgRtBuffer rt_exposure;
  if (cfg.histogram) {
    assert(cfg.exposure);
    std::tie(histogram, rt_histogram) = pass.write_compute_buffer({
        .name = "Luminance histogram",
        .buffer = cfg.histogram,
    });
    rt_exposure = pass.read_compute_buffer({
        .buffer = cfg.exposure,
        .temporal_offset = 1,
    });
  }

  PostProcessingPassResources rcs = {
      .pipeline = cfg.pipeline,
      .texture = rt_texture,
      .histogram = rt_histogram,
      .exposure = rt_exposure,
  };

  pass.set_compute_callback(ren_rg_compute_callback(RgNoPassData) {
    run_post_processing_uber_pass(device, rg, pass, rcs);
  });

  return {
      .texture = texture,
      .histogram = histogram,
  };
}

struct ReduceLuminanceHistogramPassResources {
  Handle<ComputePipeline> pipeline;
  RgRtBuffer histogram;
  RgRtBuffer exposure;
};

struct ReduceLuminanceHistogramPassData {
  float exposure_compensation;
};

void run_reduce_luminance_histogram_pass(
    Device &device, const RgRuntime &rg, ComputePass &pass,
    const ReduceLuminanceHistogramPassResources &rcs,
    const ReduceLuminanceHistogramPassData &data) {
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.set_push_constants(glsl::ReduceLuminanceHistogramConstants{
      .histogram = device.get_buffer_device_address<glsl::LuminanceHistogram>(
          rg.get_buffer(rcs.histogram)),
      .exposure = device.get_buffer_device_address<glsl::Exposure>(
          rg.get_buffer(rcs.exposure)),
      .exposure_compensation = data.exposure_compensation,
  });
  pass.dispatch_groups(1);
}

struct ReduceLuminanceHistogramPassConfig {
  Handle<ComputePipeline> pipeline;
  RgBuffer histogram;
  RgBuffer exposure;
};

auto setup_reduce_luminance_histogram_pass(
    Device &device, RenderGraph::Builder &rgb,
    const ReduceLuminanceHistogramPassConfig &cfg) -> RgPass {
  assert(cfg.pipeline);
  assert(cfg.histogram);

  auto pass = rgb.create_pass({
      .name = "Automatic exposure: reduce luminance histogram",
      .type = RgPassType::Compute,
  });

  auto rt_histogram = pass.read_compute_buffer({.buffer = cfg.histogram});

  auto [exposure, rt_exposure] = pass.create_compute_buffer({
      .name = "Automatic exposure",
      .target = cfg.exposure,
      .size = sizeof(glsl::Exposure),
      .temporal_count = 1,
      .temporal_init = TempSpan(float(1.0f / glsl::MIN_LUMINANCE)).as_bytes(),
  });

  ReduceLuminanceHistogramPassResources rcs = {
      .pipeline = cfg.pipeline,
      .histogram = rt_histogram,
      .exposure = rt_exposure,
  };

  pass.set_compute_callback(
      ren_rg_compute_callback(ReduceLuminanceHistogramPassData) {
        run_reduce_luminance_histogram_pass(device, rg, pass, rcs, data);
      });

  return pass;
}

} // namespace

auto setup_post_processing_passes(Device &device, RgBuilder &rgb,
                                  const PostProcessingPassesConfig &cfg)
    -> PostProcessingPassesOutput {
  assert(cfg.pipelines);
  assert(cfg.options);
  assert(cfg.texture);

  PostProcessingPasses passes;

  auto automatic_exposure =
      cfg.options->exposure.mode.get<ExposureOptions::Automatic>();

  RgBuffer histogram;
  if (automatic_exposure) {
    histogram = setup_initialize_luminance_histogram_pass(device, rgb);
  }

  switch (cfg.options->tone_mapping.oper) {
  case REN_TONE_MAPPING_OPERATOR_REINHARD: {
  } break;
  case REN_TONE_MAPPING_OPERATOR_ACES: {
    todo("ACES tone mapping is not implemented!");
  } break;
  }

  RgBuffer exposure;
  if (cfg.exposure.passes.automatic) {
    exposure = cfg.exposure.exposure;
  }

  auto [texture, histogram_after_build] = setup_post_processing_uber_pass(
      device, rgb,
      {
          .pipeline = cfg.pipelines->post_processing,
          .texture = cfg.texture,
          .histogram = histogram,
          .exposure = exposure,
      });
  histogram = histogram_after_build;

  if (automatic_exposure) {
    passes.reduce_luminance_histogram = setup_reduce_luminance_histogram_pass(
        device, rgb,
        {
            .pipeline = cfg.pipelines->reduce_luminance_histogram,
            .histogram = histogram,
            .exposure = exposure,
        });
  }

  return {
      .passes = passes,
      .texture = texture,
  };
}

auto set_post_processing_passes_data(RenderGraph &rg,
                                     const PostProcessingPasses &passes,
                                     const PostProcessingPassesData &data)
    -> bool {
  auto automatic_exposure =
      data.options->exposure.mode.get<ExposureOptions::Automatic>();

  if (automatic_exposure) {
    if (passes.reduce_luminance_histogram) {
      rg.set_pass_data(passes.reduce_luminance_histogram,
                       ReduceLuminanceHistogramPassData{
                           .exposure_compensation =
                               automatic_exposure->exposure_compensation});
      return true;
    }
    return false;
  }

  return true;
}

} // namespace ren
