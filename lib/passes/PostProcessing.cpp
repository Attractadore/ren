#include "PostProcessing.hpp"
#include "../CommandRecorder.hpp"
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
    histogram = ccfg.rgb->create_buffer<glsl::LuminanceHistogram>({
        .heap = BufferHeap::Static,
        .count = 1,
    });

    auto pass = ccfg.rgb->create_pass({.name = "init-luminance-histogram"});

    RgBufferToken<glsl::LuminanceHistogram> token = pass.write_buffer(
        "luminance-histogram-empty", &histogram, TRANSFER_DST_BUFFER);

    pass.set_callback(
        [token](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
          cmd.fill_buffer(BufferView(rt.get_buffer(token)), 0);
        });
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
        .hdr = pass.read_texture(cfg.hdr, CS_SAMPLE_TEXTURE),
        .sdr = pass.write_texture("sdr", cfg.sdr.get(), CS_UAV_TEXTURE),
    };

    if (histogram) {
      args.histogram = pass.write_buffer("luminance-histogram", &histogram,
                                         CS_READ_WRITE_BUFFER);
      args.previous_exposure =
          pass.read_texture(cfg.exposure, CS_SAMPLE_TEXTURE, 1);
    }

    pass.dispatch_grid_2d(ccfg.pipelines->post_processing, args, viewport,
                          {4, 4});
  }

  if (scene.exposure.mode == ExposureMode::Automatic) {
    auto pass = ccfg.rgb->create_pass({.name = "reduce-luminance-histogram"});

    RgReduceLuminanceHistogramArgs args = {
        .histogram = pass.read_buffer(histogram, CS_READ_BUFFER),
        .exposure = pass.write_texture("exposure", cfg.exposure, nullptr,
                                       CS_UAV_TEXTURE),
        .exposure_compensation = ccfg.scene->exposure.ec,
    };

    pass.dispatch_grid(ccfg.pipelines->reduce_luminance_histogram, args,
                       glsl::NUM_LUMINANCE_HISTOGRAM_BINS);
  }
}
