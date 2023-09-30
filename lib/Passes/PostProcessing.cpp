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
  RgTextureId texture;
  RgBufferId histogram;
  RgTextureId exposure;
};

void run_post_processing_uber_pass(const RgRuntime &rg, ComputePass &pass,
                                   const PostProcessingPassResources &rcs) {
  assert(rcs.pipeline);
  assert(rcs.texture);

  pass.bind_compute_pipeline(rcs.pipeline);
  pass.bind_descriptor_sets({rg.get_texture_set()});

  assert(!rcs.histogram == !rcs.exposure);
  BufferReference<glsl::LuminanceHistogram> histogram;
  StorageTextureId previous_exposure;
  if (rcs.histogram) {
    histogram = g_renderer->get_buffer_device_address<glsl::LuminanceHistogram>(
        rg.get_buffer(rcs.histogram));
    previous_exposure = rg.get_storage_texture_descriptor(rcs.exposure);
  }

  Handle<Texture> texture = rg.get_texture(rcs.texture);
  u32 texture_index = rg.get_storage_texture_descriptor(rcs.texture);

  pass.set_push_constants(glsl::PostProcessingConstants{
      .histogram = histogram,
      .previous_exposure_texture = previous_exposure,
      .tex = texture_index,
  });

  // Dispatch 1 thread per 16 work items for optimal performance
  glm::uvec2 size = g_renderer->get_texture(texture).size;
  pass.dispatch_threads(
      size / glm::uvec2(4),
      {glsl::POST_PROCESSING_THREADS_X, glsl::POST_PROCESSING_THREADS_Y});
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
    rcs.exposure = pass.read_texture("exposure", RG_CS_READ_TEXTURE, 1);
  }

  pass.set_compute_callback(ren_rg_compute_callback(RgNoPassData) {
    run_post_processing_uber_pass(rg, pass, rcs);
  });
}

struct ReduceLuminanceHistogramPassResources {
  Handle<ComputePipeline> pipeline;
  RgBufferId histogram;
  RgTextureId exposure;
};

struct ReduceLuminanceHistogramPassData {
  float exposure_compensation;
};

void run_reduce_luminance_histogram_pass(
    const RgRuntime &rg, ComputePass &pass,
    const ReduceLuminanceHistogramPassResources &rcs,
    const ReduceLuminanceHistogramPassData &data) {
  assert(rcs.pipeline);
  assert(rcs.histogram);
  assert(rcs.exposure);
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.bind_descriptor_sets({rg.get_texture_set()});
  pass.set_push_constants(glsl::ReduceLuminanceHistogramConstants{
      .histogram =
          g_renderer->get_buffer_device_address<glsl::LuminanceHistogram>(
              rg.get_buffer(rcs.histogram)),
      .exposure_texture = rg.get_storage_texture_descriptor(rcs.exposure),
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

  rcs.exposure = pass.write_texture("exposure", "automatic-exposure-init",
                                    RG_CS_WRITE_TEXTURE);

  pass.set_compute_callback(
      ren_rg_compute_callback(ReduceLuminanceHistogramPassData) {
        run_reduce_luminance_histogram_pass(rg, pass, rcs, data);
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
