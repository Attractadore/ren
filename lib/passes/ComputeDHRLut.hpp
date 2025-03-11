#pragma once
#include "Pass.hpp"

namespace ren {

struct ComputeDHRLutPassConfig {
  NotNull<RgTextureId *> dhr_lut;
};

void setup_compute_dhr_lut_pass(const PassCommonConfig &ccfg,
                                const ComputeDHRLutPassConfig &cfg);

} // namespace ren
