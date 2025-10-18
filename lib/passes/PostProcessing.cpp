#include "PostProcessing.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../core/Views.hpp"
#include "../sh/Random.h"
#include "LocalToneMappingAccumulate.comp.hpp"
#include "LocalToneMappingLLM.comp.hpp"
#include "LocalToneMappingLightness.comp.hpp"
#include "LocalToneMappingReduce.comp.hpp"
#include "PostProcessing.comp.hpp"
#include "ReduceLuminanceHistogram.comp.hpp"
#include "ren/core/Format.hpp"

void ren::setup_post_processing_passes(const PassCommonConfig &ccfg,
                                       const PostProcessingPassesConfig &cfg) {
  ScratchArena scratch(ccfg.rgb->m_arena);

  const Scene &scene = *ccfg.scene;

  RgBufferId<u32> luminance_histogram;
  if (scene.m_settings.exposure_mode == sh::EXPOSURE_MODE_AUTOMATIC) {
    luminance_histogram = ccfg.rgb->create_buffer<u32>({
        .count = sh::NUM_LUMINANCE_HISTOGRAM_BINS,
        .init = 0,
        .init_queue = RgQueue::Async,
    });
  }

  *cfg.sdr = ccfg.rcs->sdr;

  const SceneGraphicsSettings &settings = scene.m_settings;

  auto noise_lut = ccfg.allocator->allocate<glm::vec3>(
      sh::PP_HILBERT_CURVE_SIZE * sh::PP_HILBERT_CURVE_SIZE);

  for (u32 y : range(sh::PP_HILBERT_CURVE_SIZE)) {
    for (u32 x : range(sh::PP_HILBERT_CURVE_SIZE)) {
      u32 h = sh::hilbert_from_2d(sh::PP_HILBERT_CURVE_SIZE, x, y);
      noise_lut.host_ptr[y * sh::PP_HILBERT_CURVE_SIZE + x] = sh::r3_seq(h);
    }
  }

  glm::uvec2 ltm_size = ccfg.viewport / 2u;
  RgTextureId ltm_llm;
  if (settings.local_tone_mapping) {
    i32 num_mips = std::min<u32>(settings.ltm_pyramid_size,
                                 get_mip_chain_length(ltm_size.x, ltm_size.y));
    i32 llm_mip = std::min<u32>(settings.ltm_llm_mip, num_mips - 1);
    u32 pad_size = 1 << (num_mips - 1);
    ltm_size.x = pad(ltm_size.x, pad_size);
    ltm_size.y = pad(ltm_size.y, pad_size);
    if (!ccfg.rcs->ltm_lightness) {
      ccfg.rcs->ltm_lightness = ccfg.rgp->create_texture({
          .name = "ltm-lightness",
          .format = TinyImageFormat_R10G10B10A2_UNORM,
          .width = ltm_size.x,
          .height = ltm_size.y,
          .num_mips = (u32)num_mips,
      });
    }
    if (!ccfg.rcs->ltm_weights) {
      ccfg.rcs->ltm_weights = ccfg.rgp->create_texture({
          .name = "ltm-weights",
          .format = TinyImageFormat_R16G16_UNORM,
          .width = ltm_size.x,
          .height = ltm_size.y,
          .num_mips = (u32)num_mips,
      });
    }
    glm::uvec2 llm_size = get_mip_size({ltm_size, 1}, llm_mip);
    if (!ccfg.rcs->ltm_accumulator) {
      ccfg.rcs->ltm_accumulator = ccfg.rgp->create_texture({
          .name = "ltm-accumulator",
          .format = TinyImageFormat_R8_UNORM,
          .width = llm_size.x,
          .height = llm_size.y,
          .num_mips = (u32)(num_mips - llm_mip),
      });
    }
    if (!ccfg.rcs->ltm_llm) {
      ccfg.rcs->ltm_llm = ccfg.rgp->create_texture({
          .name = "ltm-llm",
          .format = TinyImageFormat_R16G16_SFLOAT,
          .width = llm_size.x,
          .height = llm_size.y,
      });
    }

    RgTextureId ltm_lightness = ccfg.rcs->ltm_lightness;
    RgTextureId ltm_weights = ccfg.rcs->ltm_weights;
    RgTextureId ltm_accumulator = ccfg.rcs->ltm_accumulator;
    ltm_llm = ccfg.rcs->ltm_llm;

    float shadows = glm::exp2(settings.ltm_shadows);
    float highlights = glm::exp2(-settings.ltm_highlights);
    {
      auto pass = ccfg.rgb->create_pass({
          .name = "local-tone-mapping-lightness",
          .queue = RgQueue::Async,
      });
      RgLocalToneMappingLightnessArgs args = {
          .hdr = pass.read_texture(cfg.hdr),
          .lightness = pass.write_texture("ltm-lightness-0", &ltm_lightness),
          .weights = pass.write_texture("ltm-weights-0", &ltm_weights),
          .middle_gray = settings.middle_gray,
          .tone_mapper = settings.tone_mapper,
          .shadows = shadows,
          .highlights = highlights,
          .sigma = settings.ltm_sigma,
      };
      pass.dispatch(ccfg.pipelines->local_tone_mapping_lightness, args,
                    ceil_div(ltm_size.x, sh::LTM_LIGHTNESS_TILE_SIZE.x),
                    ceil_div(ltm_size.y, sh::LTM_LIGHTNESS_TILE_SIZE.y));
    }
    for (u32 mip = 1; mip < num_mips; ++mip) {
      auto pass = ccfg.rgb->create_pass({
          .name = format(scratch, "local-tone-mapping-reduce-{}", mip),
          .queue = RgQueue::Async,
      });
      RgLocalToneMappingReduceArgs args = {
          .src_lightness = pass.write_texture(
              format(scratch, "ltm-lightness-{}", mip), &ltm_lightness,
              rhi::CS_UNORDERED_ACCESS_IMAGE | rhi::CS_RESOURCE_IMAGE,
              rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP, mip),
          .src_weights = pass.write_texture(
              format(scratch, "ltm-weights-{}", mip), &ltm_weights,
              rhi::CS_UNORDERED_ACCESS_IMAGE | rhi::CS_RESOURCE_IMAGE,
              rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP, mip),
          .dst_lightness = args.src_lightness,
          .dst_weights = args.src_weights,
          .src_mip = mip - 1,
      };
      glm::uvec2 mip_size = get_mip_size({ltm_size, 1}, mip);
      pass.dispatch(ccfg.pipelines->local_tone_mapping_reduce, args,
                    ceil_div(mip_size.x, sh::LTM_REDUCE_TILE_SIZE.x),
                    ceil_div(mip_size.y, sh::LTM_REDUCE_TILE_SIZE.y));
    }
    for (i32 mip = num_mips - 1; mip >= llm_mip; --mip) {
      auto pass = ccfg.rgb->create_pass({
          .name = format(scratch, "local-tone-mapping-accumulate-{}", mip),
          .queue = RgQueue::Async,
      });

      RgTextureToken accumulator = pass.write_texture(
          format(scratch, "ltm-accumulator-{}", mip), &ltm_accumulator,
          rhi::CS_UNORDERED_ACCESS_IMAGE | rhi::CS_RESOURCE_IMAGE,
          rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP, mip - llm_mip);

      RgLocalToneMappingAccumulateArgs args = {
          .lightness = pass.read_texture(ltm_lightness,
                                         rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
          .weights = pass.read_texture(ltm_weights,
                                       rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
          .src_accumulator =
              mip < num_mips - 1 ? accumulator : RgTextureToken(),
          .dst_accumulator = accumulator,
          .dst_mip = u32(mip),
          .llm_mip = u32(llm_mip),
          .contrast_boost = settings.ltm_contrast_boost,
      };
      glm::uvec2 mip_size = get_mip_size({ltm_size, 1}, mip);
      pass.dispatch(ccfg.pipelines->local_tone_mapping_accumulate, args,
                    ceil_div(mip_size.x, sh::LTM_ACCUMULATE_TILE_SIZE.x),
                    ceil_div(mip_size.y, sh::LTM_ACCUMULATE_TILE_SIZE.y));
    }
    {
      auto pass = ccfg.rgb->create_pass({
          .name = "local-tone-mapping-llm",
          .queue = RgQueue::Async,
      });
      RgLocalToneMappingLLMArgs args = {
          .lightness = pass.read_texture(ltm_lightness,
                                         rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
          .accumulator = pass.read_texture(
              ltm_accumulator, rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
          .llm = pass.write_texture("ltm-llm", &ltm_llm),
          .mip = (u32)llm_mip,
      };
      pass.dispatch(ccfg.pipelines->local_tone_mapping_llm, args,
                    ceil_div(llm_size.x, sh::LTM_LLM_TILE_SIZE.x),
                    ceil_div(llm_size.y, sh::LTM_LLM_TILE_SIZE.y));
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
    if (scene.m_settings.metering_mode == sh::METERING_MODE_CENTER_WEIGHTED) {
      inner_size =
          scene.m_settings
              .center_weighted_metering_pattern_relative_inner_diameter;
      outer_size = inner_size *
                   scene.m_settings.center_weighted_metering_pattern_size_ratio;
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
    }

    if (settings.local_tone_mapping) {
      args.ltm_llm =
          pass.read_texture(ltm_llm, rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP);
      args.ltm_inv_size = 1.0f / glm::vec2(2u * ltm_size);
    }

    u32 num_groups_x = ceil_div(ccfg.viewport.x, sh::PP_TILE_SIZE.x);
    u32 num_groups_y = ceil_div(ccfg.viewport.y, sh::PP_TILE_SIZE.y);

    pass.dispatch(ccfg.pipelines->post_processing, args, num_groups_x,
                  num_groups_y);
  }

  if (scene.m_settings.exposure_mode == sh::EXPOSURE_MODE_AUTOMATIC) {
    auto pass = ccfg.rgb->create_pass({
        .name = "reduce-luminance-histogram",
        .queue = RgQueue::Async,
    });

    bool temporal_adaptation =
        cfg.frame_index > 0 and scene.m_settings.temporal_adaptation;

    RgReduceLuminanceHistogramArgs args = {
        .luminance_histogram = pass.read_buffer(luminance_histogram),
        .exposure = pass.write_buffer("new-exposure", cfg.exposure.get()),
        .exposure_compensation = scene.m_settings.exposure_compensation,
        .dark_adaptation_time =
            temporal_adaptation ? scene.m_settings.dark_adaptation_time : 0.0f,
        .bright_adaptation_time = temporal_adaptation
                                      ? scene.m_settings.bright_adaptation_time
                                      : 0.0f,
        .dt = scene.m_delta_time,
    };

    pass.dispatch_grid(ccfg.pipelines->reduce_luminance_histogram, args,
                       sh::NUM_LUMINANCE_HISTOGRAM_BINS);
  }
}
