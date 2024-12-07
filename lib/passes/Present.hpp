#pragma once
#include "Pass.hpp"

namespace ren {

struct PresentPassConfig {
  RgTextureId src;
  Handle<Semaphore> acquire_semaphore;
  Handle<Semaphore> present_semaphore;
  NotNull<Swapchain *> swapchain;
};

void setup_present_pass(const PassCommonConfig &ccfg,
                        const PresentPassConfig &cfg);

} // namespace ren
