#include "PostProcessing.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../core/Views.hpp"
#include "../sh/Random.h"
#include "PostProcessing.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"

void ren::setup_post_processing_passes(const PassCommonConfig &ccfg,
                                       const PostProcessingPassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

  RgBufferId<float> luminance_histogram;
  if (scene.settings.exposure_mode == sh::EXPOSURE_MODE_AUTOMATIC) {
    luminance_histogram = ccfg.rgb->create_buffer<float>({
        .count = sh::NUM_LUMINANCE_HISTOGRAM_BINS,
        .init = 0.0f,
        .init_queue = RgQueue::Async,
    });
  }

  *cfg.sdr = ccfg.rcs->sdr;

  {
    auto pass = ccfg.rgb->create_pass({
        .name = "post-processing",
        .queue = RgQueue::Async,
    });

    pass.wait_semaphore(ccfg.rcs->acquire_semaphore);

    float inner_size = scene.settings.spot_metering_pattern_relative_diameter;
    float outer_size = inner_size;
    if (scene.settings.metering_mode == sh::METERING_MODE_CENTER_WEIGHTED) {
      inner_size =
          scene.settings
              .center_weighted_metering_pattern_relative_inner_diameter;
      outer_size = inner_size *
                   scene.settings.center_weighted_metering_pattern_size_ratio;
    };

    auto noise_lut = ccfg.allocator->allocate<glm::vec3>(
        sh::PP_HILBERT_CURVE_SIZE * sh::PP_HILBERT_CURVE_SIZE);

    for (u32 y : range(sh::PP_HILBERT_CURVE_SIZE)) {
      for (u32 x : range(sh::PP_HILBERT_CURVE_SIZE)) {
        u32 h = sh::hilbert_from_2d(sh::PP_HILBERT_CURVE_SIZE, x, y);
        noise_lut.host_ptr[y * sh::PP_HILBERT_CURVE_SIZE + x] = sh::r3_seq(h);
      }
    }

    RgPostProcessingArgs args = {
        .noise_lut = noise_lut.device_ptr,
        .middle_gray = scene.settings.middle_gray,
        .metering_mode = scene.settings.metering_mode,
        .metering_pattern_relative_inner_size = inner_size,
        .metering_pattern_relative_outer_size = outer_size,
        .hdr = pass.read_texture(cfg.hdr),
        .sdr = pass.write_texture("sdr", cfg.sdr.get()),
        .tone_mapper = scene.settings.tone_mapper,
        .dithering = scene.settings.dithering,
    };

    if (luminance_histogram) {
      args.luminance_histogram =
          pass.write_buffer("luminance-histogram", &luminance_histogram);
      args.exposure = pass.read_buffer(*cfg.exposure);
    }

    pass.dispatch_grid_2d(ccfg.pipelines->post_processing, args, ccfg.viewport,
                          {4, 4});
  }

  if (scene.settings.exposure_mode == sh::EXPOSURE_MODE_AUTOMATIC) {
    auto pass = ccfg.rgb->create_pass({
        .name = "reduce-luminance-histogram",
        .queue = RgQueue::Async,
    });

    bool temporal_adaptation =
        cfg.frame_index > 0 and scene.settings.temporal_adaptation;

    RgReduceLuminanceHistogramArgs args = {
        .luminance_histogram = pass.read_buffer(luminance_histogram),
        .exposure = pass.write_buffer("new-exposure", cfg.exposure.get()),
        .exposure_compensation = scene.settings.exposure_compensation,
        .dark_adaptation_time =
            temporal_adaptation ? scene.settings.dark_adaptation_time : 0.0f,
        .bright_adaptation_time =
            temporal_adaptation ? scene.settings.bright_adaptation_time : 0.0f,
        .dt = scene.delta_time,
    };

    pass.dispatch_grid(ccfg.pipelines->reduce_luminance_histogram, args,
                       sh::NUM_LUMINANCE_HISTOGRAM_BINS);
  }
}
