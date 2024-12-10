#include "PostProcessing.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Swapchain.hpp"
#include "glsl/PostProcessing.h"
#include "glsl/ReduceLuminanceHistogram.h"

namespace ren {

namespace {

auto setup_initialize_luminance_histogram_pass(RgBuilder &rgb)
    -> RgBufferId<glsl::LuminanceHistogram> {
  auto pass = rgb.create_pass({.name = "init-luminance-histogram"});

  auto histogram =
      rgb.create_buffer<glsl::LuminanceHistogram>({.heap = BufferHeap::Static});

  RgBufferToken<glsl::LuminanceHistogram> histogram_token;
  std::tie(histogram, histogram_token) = pass.write_buffer(
      "luminance-histogram-empty", histogram, TRANSFER_DST_BUFFER);

  pass.set_callback([=](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
    cmd.fill_buffer(BufferView(rt.get_buffer(histogram_token)), 0);
  });

  return histogram;
};

struct PostProcessingPassResources {
  Handle<ComputePipeline> pipeline;
  RgTextureToken hdr;
  RgTextureToken sdr;
  RgBufferToken<glsl::LuminanceHistogram> histogram;
  RgTextureToken previous_exposure;
};

void run_post_processing_uber_pass(Renderer &renderer, const RgRuntime &rg,
                                   ComputePass &pass,
                                   const PostProcessingPassResources &rcs) {
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.bind_descriptor_sets({rg.get_texture_set()});

  DevicePtr<glsl::LuminanceHistogram> histogram;
  glsl::Texture2D previous_exposure;
  if (rcs.histogram) {
    histogram = rg.get_buffer_device_ptr(rcs.histogram);
    previous_exposure =
        glsl::Texture2D(rg.get_texture_descriptor(rcs.previous_exposure));
  }

  pass.set_push_constants(glsl::PostProcessingArgs{
      .histogram = histogram,
      .previous_exposure = previous_exposure,
      .hdr = glsl::Texture2D(rg.get_texture_descriptor(rcs.hdr)),
      .sdr = glsl::StorageTexture2D(rg.get_storage_texture_descriptor(rcs.sdr)),
  });

  // Dispatch 1 thread per 16 work items for optimal performance
  pass.dispatch_threads(
      glm::uvec2(renderer.get_texture(rg.get_texture(rcs.hdr)).size) /
          glm::uvec2(4),
      {glsl::POST_PROCESSING_THREADS_X, glsl::POST_PROCESSING_THREADS_Y});
}

struct PostProcessingPassConfig {
  NotNull<RgBufferId<glsl::LuminanceHistogram> *> histogram;
  RgTextureId hdr;
  RgTextureId exposure;
  NotNull<RgTextureId *> sdr;
};

void setup_post_processing_uber_pass(const PassCommonConfig &ccfg,
                                     const PostProcessingPassConfig &cfg) {
  PostProcessingPassResources rcs;

  rcs.pipeline = ccfg.pipelines->post_processing;

  RgBuilder &rgb = *ccfg.rgb;
  const SceneData &scene = *ccfg.scene;

  auto pass = rgb.create_pass({.name = "post-processing"});

  rcs.hdr = pass.read_texture(cfg.hdr, CS_SAMPLE_TEXTURE);

  glm::uvec2 viewport = ccfg.swapchain->get_size();

  std::tie(*cfg.sdr, rcs.sdr) =
      pass.write_texture("sdr", *cfg.sdr, CS_UAV_TEXTURE);

  if (*cfg.histogram) {
    std::tie(*cfg.histogram, rcs.histogram) = pass.write_buffer(
        "luminance-histogram", *cfg.histogram, CS_READ_WRITE_BUFFER);
    rcs.previous_exposure =
        pass.read_texture(cfg.exposure, CS_SAMPLE_TEXTURE, 1);
  }

  pass.set_compute_callback(
      [rcs](Renderer &renderer, const RgRuntime &rg, ComputePass &pass) {
        run_post_processing_uber_pass(renderer, rg, pass, rcs);
      });
}

struct ReduceLuminanceHistogramPassResources {
  Handle<ComputePipeline> pipeline;
  RgBufferToken<glsl::LuminanceHistogram> histogram;
  RgTextureToken exposure;
  float ec;
};

void run_reduce_luminance_histogram_pass(
    const RgRuntime &rg, ComputePass &pass,
    const ReduceLuminanceHistogramPassResources &rcs) {
  pass.bind_compute_pipeline(rcs.pipeline);
  pass.bind_descriptor_sets({rg.get_texture_set()});
  pass.set_push_constants(glsl::ReduceLuminanceHistogramArgs{
      .histogram = rg.get_buffer_device_ptr(rcs.histogram),
      .exposure = glsl::StorageTexture2D(
          rg.get_storage_texture_descriptor(rcs.exposure)),
      .exposure_compensation = rcs.ec,
  });
  pass.dispatch_groups(1);
}

struct ReduceLuminanceHistogramPassConfig {
  RgBufferId<glsl::LuminanceHistogram> histogram;
  RgTextureId exposure;
};

void setup_reduce_luminance_histogram_pass(
    const PassCommonConfig &ccfg,
    const ReduceLuminanceHistogramPassConfig &cfg) {
  ReduceLuminanceHistogramPassResources rcs;

  rcs.pipeline = ccfg.pipelines->reduce_luminance_histogram;

  auto pass = ccfg.rgb->create_pass({.name = "reduce-luminance-histogram"});

  rcs.histogram = pass.read_buffer(cfg.histogram, CS_READ_BUFFER);

  std::tie(std::ignore, rcs.exposure) =
      pass.write_texture("exposure", cfg.exposure, CS_UAV_TEXTURE);

  rcs.ec = ccfg.scene->exposure.ec;

  pass.set_compute_callback(
      [rcs](Renderer &, const RgRuntime &rg, ComputePass &pass) {
        run_reduce_luminance_histogram_pass(rg, pass, rcs);
      });
}

} // namespace

} // namespace ren

void ren::setup_post_processing_passes(const PassCommonConfig &ccfg,
                                       const PostProcessingPassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

  RgBufferId<glsl::LuminanceHistogram> histogram;
  if (scene.exposure.mode == ExposureMode::Automatic) {
    histogram = setup_initialize_luminance_histogram_pass(*ccfg.rgb);
  }

  if (!ccfg.rcs->sdr) {
    glm::uvec2 viewport = ccfg.swapchain->get_size();
    ccfg.rcs->sdr = ccfg.rgp->create_texture({
        .name = "sdr",
        .format = SDR_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  *cfg.sdr = ccfg.rcs->sdr;

  setup_post_processing_uber_pass(ccfg, PostProcessingPassConfig{
                                            .histogram = &histogram,
                                            .hdr = cfg.hdr,
                                            .exposure = cfg.exposure,
                                            .sdr = cfg.sdr,
                                        });

  if (scene.exposure.mode == ExposureMode::Automatic) {
    setup_reduce_luminance_histogram_pass(ccfg,
                                          ReduceLuminanceHistogramPassConfig{
                                              .histogram = histogram,
                                              .exposure = cfg.exposure,
                                          });
  }
}
