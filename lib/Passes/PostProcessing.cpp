#include "Passes/PostProcessing.hpp"
#include "CommandRecorder.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"
#include "glsl/PostProcessingPass.hpp"
#include "glsl/ReduceLuminanceHistogramPass.hpp"

using namespace ren;

namespace {

void setup_initialize_luminance_histogram_pass(RgBuilder &rgb) {
  auto pass = rgb.create_pass("init-luminance-histogram");

  auto histogram = pass.create_buffer(
      {
          .name = "luminance-histogram-empty",
          .size = sizeof(glsl::LuminanceHistogram),
      },
      RG_TRANSFER_DST_BUFFER);

  pass.set_transfer_callback(ren_rg_transfer_callback(RgNoPassData) {
    cmd.fill_buffer(rg.get_buffer(histogram), 0);
  });
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
  assert(rcs.pipeline);
  assert(rcs.texture);

  pass.bind_compute_pipeline(rcs.pipeline);

  assert(!rcs.histogram == !rcs.exposure);
  BufferReference<glsl::LuminanceHistogram> histogram;
  BufferReference<glsl::Exposure> previous_exposure;
  if (rcs.histogram) {
    histogram = device.get_buffer_device_address<glsl::LuminanceHistogram>(
        rg.get_buffer(rcs.histogram));
    previous_exposure = device.get_buffer_device_address<glsl::Exposure>(
        rg.get_buffer(rcs.exposure));
  }

  Handle<Texture> texture = rg.get_texture(rcs.texture);
  u32 texture_index = rg.get_storage_texture_descriptor(rcs.texture);

  pass.set_push_constants(glsl::PostProcessingConstants{
      .histogram = histogram,
      .previous_exposure = previous_exposure,
      .tex = texture_index,
  });

  glm::uvec2 size = device.get_texture(texture).size;
  glm::uvec2 group_size = {glsl::POST_PROCESSING_THREADS_X,
                           glsl::POST_PROCESSING_THREADS_Y};
  glm::uvec2 work_size = {glsl::POST_PROCESSING_WORK_SIZE_X,
                          glsl::POST_PROCESSING_WORK_SIZE_Y};
  pass.dispatch_threads(size, group_size * work_size);
}

struct PostProcessingPassConfig {
  Handle<ComputePipeline> pipeline;
};

void setup_post_processing_uber_pass(RgBuilder &rgb,
                                     const PostProcessingPassConfig &cfg) {
  assert(cfg.pipeline);

  PostProcessingPassResources rcs;
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass("post-processing");

  rcs.texture = pass.write_texture("pp-color-buffer", "color-buffer",
                                   RG_CS_READ_WRITE_TEXTURE);

  if (rgb.is_buffer_valid("luminance-histogram-empty")) {
    rcs.histogram =
        pass.write_buffer("luminance-histogram", "luminance-histogram-empty",
                          RG_CS_READ_WRITE_BUFFER);
    rcs.exposure = pass.read_buffer("exposure", RG_CS_READ_BUFFER, 1);
  }

  pass.set_compute_callback(ren_rg_compute_callback(RgNoPassData) {
    run_post_processing_uber_pass(device, rg, pass, rcs);
  });
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
  assert(rcs.pipeline);
  assert(rcs.histogram);
  assert(rcs.exposure);
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
};

void setup_reduce_luminance_histogram_pass(
    RgBuilder &rgb, const ReduceLuminanceHistogramPassConfig &cfg) {
  assert(cfg.pipeline);

  ReduceLuminanceHistogramPassResources rcs;
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass("reduce-luminance-histogram");

  rcs.histogram = pass.read_buffer("luminance-histogram", RG_CS_READ_BUFFER);

  rcs.exposure = pass.write_buffer("exposure", "automatic-exposure-init",
                                   RG_CS_WRITE_BUFFER);

  pass.set_compute_callback(
      ren_rg_compute_callback(ReduceLuminanceHistogramPassData) {
        run_reduce_luminance_histogram_pass(device, rg, pass, rcs, data);
      });
}

} // namespace

void ren::setup_post_processing_passes(RgBuilder &rgb,
                                       const PostProcessingPassesConfig &cfg) {
  assert(cfg.pipelines);
  assert(cfg.options);

  auto automatic_exposure =
      cfg.options->exposure.mode.get<ExposureOptions::Automatic>();

  if (automatic_exposure) {
    setup_initialize_luminance_histogram_pass(rgb);
  }

  switch (cfg.options->tone_mapping.oper) {
  case REN_TONE_MAPPING_OPERATOR_REINHARD: {
  } break;
  case REN_TONE_MAPPING_OPERATOR_ACES: {
    todo("ACES tone mapping is not implemented!");
  } break;
  }

  setup_post_processing_uber_pass(rgb,
                                  {.pipeline = cfg.pipelines->post_processing});

  if (automatic_exposure) {
    setup_reduce_luminance_histogram_pass(
        rgb, {.pipeline = cfg.pipelines->reduce_luminance_histogram});
  }
}

auto ren::set_post_processing_passes_data(RenderGraph &rg,
                                          const PostProcessingOptions &opts)
    -> bool {
  bool valid = true;

  auto automatic_exposure =
      opts.exposure.mode.get<ExposureOptions::Automatic>();

  if (automatic_exposure) {
    valid =
        rg.set_pass_data("reduce-luminance-histogram",
                         ReduceLuminanceHistogramPassData{
                             .exposure_compensation =
                                 automatic_exposure->exposure_compensation});
    if (!valid) {
      return false;
    }
  }

  return true;
}
