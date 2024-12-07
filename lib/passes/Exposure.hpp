#pragma once
#include "Pass.hpp"
#include "core/StdDef.hpp"

namespace ren {

struct ExposurePassConfig {
  NotNull<RgTextureId *> exposure;
  NotNull<u32 *> temporal_layer;
};

void setup_exposure_pass(const PassCommonConfig &ccfg,
                         const ExposurePassConfig &cfg);

} // namespace ren
