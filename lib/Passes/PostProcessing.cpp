#include "Passes/PostProcessing.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "glsl/PostProcessingPass.h"
#include "glsl/ReduceLuminanceHistogramPass.h"

namespace ren {

namespace {

auto setup_initialize_luminance_histogram_pass(RgBuilder &rgb) -> RgBufferId {
  auto pass = rgb.create_pass({.name = "init-luminance-histogram"});

  auto [histogram, histogram_token] = pass.create_buffer(
      {
          .name = "luminance-histogram-empty",
          .size = sizeof(glsl::LuminanceHistogram),
      },
      RG_TRANSFER_DST_BUFFER);

  pass.set_callback([=](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
    cmd.fill_buffer(rt.get_buffer(histogram_token), 0);
  });

  return histogram;
};

struct PostProcessingPassResources {
  RgTextureToken hdr;
  RgTextureToken sdr;
  RgBufferToken histogram;
  RgTextureToken previous_exposure;
};

void run_post_processing_uber_pass(Renderer &renderer, const RgRuntime &rg,
                                   const Scene &scene, ComputePass &pass,
                                   const PostProcessingPassResources &rcs) {
  pass.bind_compute_pipeline(scene.get_pipelines().post_processing);
  pass.bind_descriptor_sets({rg.get_texture_set()});

  DevicePtr<glsl::LuminanceHistogram> histogram;
  StorageTextureId previous_exposure;
  if (rcs.histogram) {
    histogram =
        rg.get_buffer_device_ptr<glsl::LuminanceHistogram>(rcs.histogram),
    previous_exposure =
        rg.get_storage_texture_descriptor(rcs.previous_exposure);
  }

  pass.set_push_constants(glsl::PostProcessingPassArgs{
      .histogram = histogram,
      .previous_exposure_texture = previous_exposure,
      .hdr_texture = rg.get_storage_texture_descriptor(rcs.hdr),
      .sdr_texture = rg.get_storage_texture_descriptor(rcs.sdr),
  });

  // Dispatch 1 thread per 16 work items for optimal performance
  pass.dispatch_threads(
      glm::uvec2(renderer.get_texture(rg.get_texture(rcs.hdr)).size) /
          glm::uvec2(4),
      {glsl::POST_PROCESSING_THREADS_X, glsl::POST_PROCESSING_THREADS_Y});
}

struct PostProcessingPassConfig {
  RgBufferId histogram;
  RgTextureId hdr;
  RgTextureId exposure;
};

struct PostProcessingPassOutput {
  RgBufferId histogram;
  RgTextureId sdr;
};

auto setup_post_processing_uber_pass(RgBuilder &rgb,
                                     NotNull<const Scene *> scene,
                                     const PostProcessingPassConfig &cfg)
    -> PostProcessingPassOutput {
  PostProcessingPassResources rcs;
  PostProcessingPassOutput out;

  auto pass = rgb.create_pass({.name = "post-processing"});

  rcs.hdr = pass.read_texture(cfg.hdr, RG_CS_READ_TEXTURE);

  glm::uvec2 viewport = scene->get_viewport();

  std::tie(out.sdr, rcs.sdr) = pass.create_texture(
      {
          .name = "sdr",
          .format = SDR_FORMAT,
          .width = viewport.x,
          .height = viewport.y,
      },
      RG_CS_WRITE_TEXTURE);

  if (cfg.histogram) {
    std::tie(out.histogram, rcs.histogram) = pass.write_buffer(
        "luminance-histogram", cfg.histogram, RG_CS_READ_WRITE_BUFFER);
    rcs.previous_exposure =
        pass.read_texture(cfg.exposure, RG_CS_READ_TEXTURE, 1);
  }

  pass.set_compute_callback(
      [=](Renderer &renderer, const RgRuntime &rt, ComputePass &pass) {
        run_post_processing_uber_pass(renderer, rt, *scene, pass, rcs);
      });

  return out;
}

struct ReduceLuminanceHistogramPassResources {
  RgBufferToken histogram;
  RgTextureToken exposure;
};

void run_reduce_luminance_histogram_pass(
    const RgRuntime &rg, const Scene &scene, ComputePass &pass,
    const ReduceLuminanceHistogramPassResources &rcs) {
  pass.bind_compute_pipeline(scene.get_pipelines().reduce_luminance_histogram);
  pass.bind_descriptor_sets({rg.get_texture_set()});
  pass.set_push_constants(glsl::ReduceLuminanceHistogramPassArgs{
      .histogram =
          rg.get_buffer_device_ptr<glsl::LuminanceHistogram>(rcs.histogram),
      .exposure_texture = rg.get_storage_texture_descriptor(rcs.exposure),
      .exposure_compensation = scene.get_exposure_compensation(),
  });
  pass.dispatch_groups(1);
}

struct ReduceLuminanceHistogramPassConfig {
  RgBufferId histogram;
  RgTextureId exposure;
};

void setup_reduce_luminance_histogram_pass(
    RgBuilder &rgb, NotNull<const Scene *> scene,
    const ReduceLuminanceHistogramPassConfig &cfg) {
  ReduceLuminanceHistogramPassResources rcs;

  auto pass = rgb.create_pass({.name = "reduce-luminance-histogram"});

  rcs.histogram = pass.read_buffer(cfg.histogram, RG_CS_READ_BUFFER);

  std::tie(std::ignore, rcs.exposure) =
      pass.write_texture("new-exposure", cfg.exposure, RG_CS_WRITE_TEXTURE);

  pass.set_compute_callback(
      [=](Renderer &, const RgRuntime &rt, ComputePass &pass) {
        run_reduce_luminance_histogram_pass(rt, *scene, pass, rcs);
      });
}

} // namespace

auto setup_post_processing_passes(RgBuilder &rgb, NotNull<const Scene *> scene,
                                  const PostProcessingPassesConfig &cfg)
    -> RgTextureId {
  ExposureMode exposure_mode = scene->get_exposure_mode();

  RgBufferId histogram;
  if (exposure_mode == ExposureMode::Automatic) {
    histogram = setup_initialize_luminance_histogram_pass(rgb);
  }

  PostProcessingPassOutput pp =
      setup_post_processing_uber_pass(rgb, scene,
                                      PostProcessingPassConfig{
                                          .histogram = histogram,
                                          .hdr = cfg.hdr,
                                          .exposure = cfg.exposure,
                                      });

  if (exposure_mode == ExposureMode::Automatic) {
    setup_reduce_luminance_histogram_pass(rgb, scene,
                                          ReduceLuminanceHistogramPassConfig{
                                              .histogram = pp.histogram,
                                              .exposure = cfg.exposure,
                                          });
  }

  return pp.sdr;
}

} // namespace ren
