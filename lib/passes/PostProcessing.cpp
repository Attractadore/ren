#include "PostProcessing.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "PostProcessing.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"

void ren::setup_post_processing_passes(const PassCommonConfig &ccfg,
                                       const PostProcessingPassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

  RgBufferId<glsl::LuminanceHistogram> histogram;
  if (scene.exposure.mode == ExposureMode::Automatic) {
    histogram = ccfg.rgb->create_buffer<glsl::LuminanceHistogram>({
        .init = glsl::LuminanceHistogram(),
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
    };

    if (histogram) {
      args.histogram = pass.write_buffer("luminance-histogram", &histogram);
      args.exposure = pass.read_buffer(*cfg.exposure);
    }

    pass.dispatch_grid_2d(ccfg.pipelines->post_processing, args, ccfg.viewport,
                          {4, 4});
  }

  if (scene.exposure.mode == ExposureMode::Automatic) {
    auto pass = ccfg.rgb->create_pass({
        .name = "reduce-luminance-histogram",
        .queue = RgQueue::Async,
    });

    RgReduceLuminanceHistogramArgs args = {
        .histogram = pass.read_buffer(histogram),
        .exposure = pass.write_buffer("new-exposure", cfg.exposure.get()),
        .exposure_compensation = ccfg.scene->exposure.ec,
    };

    pass.dispatch_grid(ccfg.pipelines->reduce_luminance_histogram, args,
                       glsl::NUM_LUMINANCE_HISTOGRAM_BINS);
  }
}
