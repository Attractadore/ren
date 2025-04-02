#include "HiZ.hpp"
#include "../Scene.hpp"
#include "../Swapchain.hpp"
#include "HiZSpd.comp.hpp"

#include <bit>

namespace ren {

void setup_hi_z_pass(const PassCommonConfig &ccfg, const HiZPassConfig &cfg) {
  glm::uvec2 viewport = ccfg.swapchain->get_size();
  glm::uvec2 size = {std::bit_floor(viewport.x), std::bit_floor(viewport.y)};
  u32 num_mips =
      std::max(std::countr_zero(size.x), std::countr_zero(size.y)) + 1;
  ren_assert(size.x < glsl::HI_Z_SPD_MAX_SIZE and
             size.y < glsl::HI_Z_SPD_MAX_SIZE);

  if (!ccfg.rcs->hi_z) {
    ccfg.rcs->hi_z = ccfg.rgp->create_texture({
        .name = "hi-z",
        .format = TinyImageFormat_R32_SFLOAT,
        .width = size.x,
        .height = size.y,
        .num_mips = num_mips,
    });
  }

  auto counter = ccfg.rgb->create_buffer<u32>({.init = 0});

  auto pass = ccfg.rgb->create_pass({.name = "hi-z-spd"});

  RgHiZSpdArgs args = {
      .counter = pass.write_buffer("hi-z-spd-counter", &counter),
      .dsts = pass.write_texture("hi-z", ccfg.rcs->hi_z, cfg.hi_z),
      .dst_size = size,
      .num_dst_mips = num_mips,
      .src = pass.read_texture(cfg.depth_buffer, ccfg.samplers->hi_z_gen),
  };

  pass.dispatch_grid_2d(
      ccfg.pipelines->hi_z, args, size,
      {glsl::HI_Z_SPD_THREAD_ELEMS_X, glsl::HI_Z_SPD_THREAD_ELEMS_Y});
}

} // namespace ren
