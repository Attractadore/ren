#pragma once
#include "../core/StdDef.hpp"
#include "Pass.hpp"

namespace ren {

struct ExposurePassConfig {
  NotNull<RgTextureId *> exposure;
  NotNull<u32 *> temporal_layer;
};

void setup_exposure_pass(const PassCommonConfig &ccfg,
                         const ExposurePassConfig &cfg);

} // namespace ren
