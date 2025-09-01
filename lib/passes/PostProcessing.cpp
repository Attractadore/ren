#include "PostProcessing.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "PostProcessing.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"

void ren::setup_post_processing_passes(const PassCommonConfig &ccfg,
                                       const PostProcessingPassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

  RgBufferId<sh::LuminanceHistogram> histogram;
  if (scene.settings.exposure_mode == sh::EXPOSURE_MODE_AUTOMATIC) {
    histogram = ccfg.rgb->create_buffer<sh::LuminanceHistogram>({
        .init = sh::LuminanceHistogram(),
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

    RgPostProcessingArgs args = {
        .hdr = pass.read_texture(cfg.hdr),
        .sdr = pass.write_texture("sdr", cfg.sdr.get()),
        .tone_mapper = scene.settings.tone_mapper,
        .output_color_space = sh::COLOR_SPACE_SRGB,
    };

    if (histogram) {
      args.histogram = pass.write_buffer("luminance-histogram", &histogram);
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

    const SceneData *scene = ccfg.scene;

    RgReduceLuminanceHistogramArgs args = {
        .histogram = pass.read_buffer(histogram),
        .exposure = pass.write_buffer("new-exposure", cfg.exposure.get()),
        .exposure_compensation = scene->settings.exposure_compensation,
        .dark_adaptation_time =
            cfg.frame_index > 0 ? scene->settings.dark_adaptation_time : 0.0f,
        .bright_adaptation_time =
            cfg.frame_index > 0 ? scene->settings.bright_adaptation_time : 0.0f,
        .dt = scene->delta_time,
    };

    pass.dispatch_grid(ccfg.pipelines->reduce_luminance_histogram, args,
                       sh::NUM_LUMINANCE_HISTOGRAM_BINS);
  }
}
