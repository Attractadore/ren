#include "PostProcessing.hpp"
#include "../Formats.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../Swapchain.hpp"
#include "PostProcessing.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"

void ren::setup_post_processing_passes(const PassCommonConfig &ccfg,
                                       const PostProcessingPassesConfig &cfg) {
  const SceneData &scene = *ccfg.scene;

  RgBufferId<glsl::LuminanceHistogram> histogram;
  if (scene.exposure.mode == ExposureMode::Automatic) {
    histogram = ccfg.rgb->create_buffer<glsl::LuminanceHistogram>(
        {.init = glsl::LuminanceHistogram()});
  }

  glm::uvec2 viewport = ccfg.swapchain->get_size();

  if (!ccfg.rcs->sdr) {
    ccfg.rcs->sdr = ccfg.rgp->create_texture({
        .name = "sdr",
        .format = SDR_FORMAT,
        .width = viewport.x,
        .height = viewport.y,
    });
  }
  *cfg.sdr = ccfg.rcs->sdr;

  {
    auto pass = ccfg.rgb->create_pass({.name = "post-processing"});

    RgPostProcessingArgs args = {
        .hdr = pass.read_texture(cfg.hdr),
        .sdr = pass.write_texture("sdr", cfg.sdr.get()),
    };

    if (histogram) {
      args.histogram = pass.write_buffer("luminance-histogram", &histogram);
      args.previous_exposure = pass.read_texture(cfg.exposure, 1);
    }

    pass.dispatch_grid_2d(ccfg.pipelines->post_processing, args, viewport,
                          {4, 4});
  }

  if (scene.exposure.mode == ExposureMode::Automatic) {
    auto pass = ccfg.rgb->create_pass({.name = "reduce-luminance-histogram"});

    RgReduceLuminanceHistogramArgs args = {
        .histogram = pass.read_buffer(histogram),
        .exposure = pass.write_texture("exposure", cfg.exposure, nullptr),
        .exposure_compensation = ccfg.scene->exposure.ec,
    };

    pass.dispatch_grid(ccfg.pipelines->reduce_luminance_histogram, args,
                       glsl::NUM_LUMINANCE_HISTOGRAM_BINS);
  }
}
