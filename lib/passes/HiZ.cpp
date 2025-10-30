#include "HiZ.hpp"
#include "HiZ.comp.hpp"
#include "PipelineLoading.hpp"

#include <bit>

namespace ren {

void setup_hi_z_pass(const PassCommonConfig &ccfg, const HiZPassConfig &cfg) {
  glm::uvec2 size = {std::bit_floor(ccfg.viewport.x),
                     std::bit_floor(ccfg.viewport.y)};
  u32 num_mips = max(std::countr_zero(size.x), std::countr_zero(size.y)) + 1;
  ren_assert(size.x < sh::SPD_MAX_SIZE and size.y < sh::SPD_MAX_SIZE);

  if (!ccfg.rcs->hi_z) {
    ccfg.rcs->hi_z = ccfg.rgp->create_texture({
        .name = "hi-z",
        .format = TinyImageFormat_R32_SFLOAT,
        .width = size.x,
        .height = size.y,
        .num_mips = num_mips,
    });
  }
  *cfg.hi_z = ccfg.rcs->hi_z;

  auto counter = ccfg.rgb->create_buffer<u32>({.init = 0});

  auto pass = ccfg.rgb->create_pass({.name = "hi-z"});

  RgHiZArgs args = {
      .spd_counter = pass.write_buffer("hi-z-spd-counter", &counter),
      .num_mips = num_mips,
      .dsts = pass.write_texture("hi-z", cfg.hi_z),
      .src = pass.read_texture(
          cfg.depth_buffer,
          {
              .mag_filter = rhi::Filter::Linear,
              .min_filter = rhi::Filter::Linear,
              .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
              .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
              .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
              .reduction_mode = rhi::SamplerReductionMode::Min,
          }),
  };

  pass.dispatch_grid_2d(ccfg.pipelines->hi_z, args, size,
                        {sh::SPD_THREAD_ELEMS_X, sh::SPD_THREAD_ELEMS_Y});
}

} // namespace ren
