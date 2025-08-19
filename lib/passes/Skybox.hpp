#pragma once
#include "Pass.hpp"

namespace ren {

struct SkyboxPassConfig {
  RgBufferId<float> exposure;
  NotNull<RgTextureId *> hdr;
  RgTextureId depth_buffer;
};

void setup_skybox_pass(const PassCommonConfig &ccfg,
                       const SkyboxPassConfig &cfg);

} // namespace ren
