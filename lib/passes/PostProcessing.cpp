#include "PostProcessing.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../core/Views.hpp"
#include "../sh/Random.h"
#include "LocalToneMapping.comp.hpp"
#include "LocalToneMappingAccumulate.comp.hpp"
#include "PostProcessing.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"

#include <fmt/format.h>

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

  const SceneGraphicsSettings &settings = scene.settings;

  auto noise_lut = ccfg.allocator->allocate<glm::vec3>(
      sh::PP_HILBERT_CURVE_SIZE * sh::PP_HILBERT_CURVE_SIZE);

  for (u32 y : range(sh::PP_HILBERT_CURVE_SIZE)) {
    for (u32 x : range(sh::PP_HILBERT_CURVE_SIZE)) {
      u32 h = sh::hilbert_from_2d(sh::PP_HILBERT_CURVE_SIZE, x, y);
      noise_lut.host_ptr[y * sh::PP_HILBERT_CURVE_SIZE + x] = sh::r3_seq(h);
    }
  }

  RgTextureId ltm_accumulator;
  if (settings.local_tone_mapping) {
    glm::uvec2 size = ccfg.viewport;
    u32 num_mips =
        std::min(sh::LTM_PYRAMID_SIZE, get_mip_chain_length(size.x, size.y));
    if (!ccfg.rcs->ltm_lightness) {
      ccfg.rcs->ltm_lightness = ccfg.rgp->create_texture({
          .name = "ltm-lightness",
          .format = TinyImageFormat_R10G10B10A2_UNORM,
          .width = size.x,
          .height = size.y,
          .num_mips = num_mips,
      });
    }
    if (!ccfg.rcs->ltm_weights) {
      ccfg.rcs->ltm_weights = ccfg.rgp->create_texture({
          .name = "ltm-weights",
          .format = TinyImageFormat_R16G16_UNORM,
          .width = size.x,
          .height = size.y,
          .num_mips = num_mips,
      });
    }
    if (!ccfg.rcs->ltm_accumulator[0]) {
      for (u32 mip : range(sh::LTM_PYRAMID_SIZE)) {
        glm::uvec2 mip_size = get_mip_size({size, 1}, mip);
        ccfg.rcs->ltm_accumulator[mip] = ccfg.rgp->create_texture({
            .name = fmt::format("ltm-accumulator-{}", mip),
            .format = TinyImageFormat_R8_UNORM,
            .width = mip_size.x,
            .height = mip_size.y,
        });
      }
    }
    RgTextureId ltm_lightness = ccfg.rcs->ltm_lightness;
    RgTextureId ltm_weights = ccfg.rcs->ltm_weights;

    {
      auto pass = ccfg.rgb->create_pass({
          .name = "local-tone-mapping",
          .queue = RgQueue::Async,
      });
      RgLocalToneMappingArgs args = {
          .exposure = pass.read_buffer(*cfg.exposure),
          .noise_lut = noise_lut.device_ptr,
          .hdr = pass.read_texture(cfg.hdr),
          .lightness = pass.write_texture("ltm-lightness", &ltm_lightness),
          .weights = pass.write_texture("ltm-weights", &ltm_weights),
          .middle_gray = settings.middle_gray,
          .tone_mapper = settings.tone_mapper,
          .shadows = glm::exp2(settings.ltm_shadows),
          .highlights = glm::exp2(-settings.ltm_highlights),
          .sigma = settings.ltm_sigma,
      };
      pass.dispatch_grid_2d(ccfg.pipelines->local_tone_mapping, args,
                            ccfg.viewport, sh::LTM_UNROLL);
    }
    for (i32 mip = num_mips - 1; mip >= 0; --mip) {
      auto pass = ccfg.rgb->create_pass({
          .name = fmt::format("local-tone-mapping-accumulate-{}", mip),
          .queue = RgQueue::Async,
      });
      RgTextureId prev_accumulator = ltm_accumulator;
      ltm_accumulator = ccfg.rcs->ltm_accumulator[mip];
      RgLocalToneMappingAccumulateArgs args = {
          .lightness = pass.read_texture(ltm_lightness,
                                         rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
          .weights = pass.read_texture(ltm_weights,
                                       rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
          .src_accumulator =
              prev_accumulator
                  ? pass.read_texture(prev_accumulator,
                                      rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP)
                  : RgTextureToken(),
          .dst_accumulator = pass.write_texture(
              fmt::format("ltm-accumulator-{}", mip), &ltm_accumulator),
          .mip = u32(mip),
          .contrast_boost = settings.ltm_contrast_boost,
      };
      pass.dispatch_grid_2d(ccfg.pipelines->local_tone_mapping_accumulate, args,
                            get_mip_size({ccfg.viewport, 1}, mip));
    }
  }

  {
    auto pass = ccfg.rgb->create_pass({
        .name = "post-processing",
        .queue = RgQueue::Async,
    });

    pass.wait_semaphore(ccfg.rcs->acquire_semaphore);

    float inner_size = settings.spot_metering_pattern_relative_diameter;
    float outer_size = inner_size;
    if (scene.settings.metering_mode == sh::METERING_MODE_CENTER_WEIGHTED) {
      inner_size =
          scene.settings
              .center_weighted_metering_pattern_relative_inner_diameter;
      outer_size = inner_size *
                   scene.settings.center_weighted_metering_pattern_size_ratio;
    };

    RgPostProcessingArgs args = {
        .noise_lut = noise_lut.device_ptr,
        .middle_gray = settings.middle_gray,
        .metering_mode = settings.metering_mode,
        .metering_pattern_relative_inner_size = inner_size,
        .metering_pattern_relative_outer_size = outer_size,
        .hdr = pass.read_texture(cfg.hdr),
        .sdr = pass.write_texture("sdr", cfg.sdr.get()),
        .tone_mapper = settings.tone_mapper,
        .dithering = settings.dithering,
    };

    if (luminance_histogram) {
      args.luminance_histogram =
          pass.write_buffer("luminance-histogram", &luminance_histogram);
      args.exposure = pass.read_buffer(*cfg.exposure);
    }

    if (settings.local_tone_mapping) {
      args.ltm_accumulator = pass.read_texture(ltm_accumulator);
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
