#pragma once
#include "Pass.hpp"

namespace ren {

struct OpaquePassesConfig {
  RgTextureId exposure;
  u32 exposure_temporal_layer = 0;
  NotNull<RgTextureId *> hdr;
};

void setup_opaque_passes(const PassCommonConfig &ccfg,
                         const OpaquePassesConfig &cfg);

} // namespace ren
