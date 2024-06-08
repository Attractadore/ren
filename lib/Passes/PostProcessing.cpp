#include "Passes/PostProcessing.hpp"
#include "CommandRecorder.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "glsl/PostProcessingPass.h"
#include "glsl/ReduceLuminanceHistogramPass.h"

namespace ren {

namespace {

void setup_initialize_luminance_histogram_pass(RgBuilder &rgb) {
  auto pass = rgb.create_pass("init-luminance-histogram");

  auto histogram = pass.create_buffer(
      {
          .name = "luminance-histogram-empty",
          .size = sizeof(glsl::LuminanceHistogramRef),
      },
      RG_TRANSFER_DST_BUFFER);

  pass.set_callback([=](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
    cmd.fill_buffer(rt.get_buffer(histogram), 0);
  });
};

struct PostProcessingPassResources {
  Handle<ComputePipeline> pipeline;
  glm::uvec2 size;
  RgTextureId hdr;
  RgTextureId sdr;
  RgBufferId histogram;
  RgTextureId exposure;
};

void run_post_processing_uber_pass(Renderer &renderer, const RgRuntime &rg,
                                   ComputePass &pass,
                                   const PostProcessingPassResources &rcs) {
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.bind_descriptor_sets({rg.get_texture_set()});

  assert(!rcs.histogram == !rcs.exposure);
  BufferReference<glsl::LuminanceHistogramRef> histogram;
  StorageTextureId previous_exposure;
  if (rcs.histogram) {
    histogram = renderer.get_buffer_device_address<glsl::LuminanceHistogramRef>(
        rg.get_buffer(rcs.histogram));
    previous_exposure = rg.get_storage_texture_descriptor(rcs.exposure);
  }

  pass.set_push_constants(glsl::PostProcessingPassArgs{
      .histogram = histogram,
      .previous_exposure_texture = previous_exposure,
      .hdr_texture = rg.get_storage_texture_descriptor(rcs.hdr),
      .sdr_texture = rg.get_storage_texture_descriptor(rcs.sdr),
  });

  // Dispatch 1 thread per 16 work items for optimal performance
  pass.dispatch_threads(
      rcs.size / glm::uvec2(4),
      {glsl::POST_PROCESSING_THREADS_X, glsl::POST_PROCESSING_THREADS_Y});
}

struct PostProcessingPassConfig {
  Handle<ComputePipeline> pipeline;
  glm::uvec2 size;
};

void setup_post_processing_uber_pass(RgBuilder &rgb,
                                     const PostProcessingPassConfig &cfg) {
  assert(cfg.pipeline);

  PostProcessingPassResources rcs;
  rcs.pipeline = cfg.pipeline;

  auto pass = rgb.create_pass("post-processing");

  rcs.hdr = pass.read_texture("hdr", RG_CS_READ_TEXTURE);
  rcs.sdr = pass.create_texture(
      {
          .name = "sdr",
          .format = SDR_FORMAT,
          .width = cfg.size.x,
          .height = cfg.size.y,
      },
      RG_CS_WRITE_TEXTURE);
  rcs.size = cfg.size;

  if (rgb.is_buffer_valid("luminance-histogram-empty")) {
    rcs.histogram =
        pass.write_buffer("luminance-histogram", "luminance-histogram-empty",
                          RG_CS_READ_WRITE_BUFFER);
    rcs.exposure = pass.read_texture("exposure", RG_CS_READ_TEXTURE, 1);
  }

  pass.set_compute_callback(
      [=](Renderer &renderer, const RgRuntime &rt, ComputePass &pass) {
        run_post_processing_uber_pass(renderer, rt, pass, rcs);
      });
}

struct ReduceLuminanceHistogramPassResources {
  Handle<ComputePipeline> pipeline;
  RgParameterId cfg;
  RgBufferId histogram;
  RgTextureId exposure;
};

struct ReduceLuminanceHistogramPassData {
  float exposure_compensation;
};

void run_reduce_luminance_histogram_pass(
    Renderer &renderer, const RgRuntime &rg, ComputePass &pass,
    const ReduceLuminanceHistogramPassResources &rcs) {
  assert(rcs.pipeline);
  assert(rcs.histogram);
  assert(rcs.exposure);
  const auto &cfg = rg.get_parameter<AutomaticExposureRuntimeConfig>(rcs.cfg);
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.bind_descriptor_sets({rg.get_texture_set()});
  pass.set_push_constants(glsl::ReduceLuminanceHistogramPassArgs{
      .histogram =
          renderer.get_buffer_device_address<glsl::LuminanceHistogramRef>(
              rg.get_buffer(rcs.histogram)),
      .exposure_texture = rg.get_storage_texture_descriptor(rcs.exposure),
      .exposure_compensation = cfg.ec,
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

  rcs.cfg = pass.read_parameter(AUTOMATIC_EXPOSURE_RUNTIME_CONFIG);

  rcs.histogram = pass.read_buffer("luminance-histogram", RG_CS_READ_BUFFER);

  rcs.exposure = pass.write_texture("exposure", "automatic-exposure-init",
                                    RG_CS_WRITE_TEXTURE);

  pass.set_compute_callback(
      [=](Renderer &renderer, const RgRuntime &rt, ComputePass &pass) {
        run_reduce_luminance_histogram_pass(renderer, rt, pass, rcs);
      });
}

} // namespace

void setup_post_processing_passes(RgBuilder &rgb,
                                  const PostProcessingPassesConfig &cfg) {
  assert(cfg.pipelines);

  if (cfg.exposure_mode == ExposureMode::Automatic) {
    setup_initialize_luminance_histogram_pass(rgb);
  }

  setup_post_processing_uber_pass(
      rgb, PostProcessingPassConfig{
               .pipeline = cfg.pipelines->post_processing,
               .size = cfg.viewport,
           });

  if (cfg.exposure_mode == ExposureMode::Automatic) {
    setup_reduce_luminance_histogram_pass(
        rgb, ReduceLuminanceHistogramPassConfig{
                 .pipeline = cfg.pipelines->reduce_luminance_histogram});
  }
}

} // namespace ren
