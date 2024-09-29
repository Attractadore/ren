#pragma once
#include "Pass.hpp"

namespace ren {

struct HiZPassConfig {
  RgTextureId depth_buffer;
  NotNull<RgTextureId *> hi_z;
};

void setup_hi_z_pass(const PassCommonConfig &ccfg, const HiZPassConfig &cfg);

} // namespace ren
