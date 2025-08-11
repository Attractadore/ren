#pragma once
#include "Pass.hpp"

namespace ren {

struct PresentPassConfig {
  RgTextureId src;
  Handle<Semaphore> acquire_semaphore;
  NotNull<SwapChain *> swap_chain;
};

void setup_present_pass(const PassCommonConfig &ccfg,
                        const PresentPassConfig &cfg);

} // namespace ren
