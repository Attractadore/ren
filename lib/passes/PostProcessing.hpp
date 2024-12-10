#pragma once
#include "Pass.hpp"

namespace ren {

struct PostProcessingPassesConfig {
  RgTextureId hdr;
  RgTextureId exposure;
  NotNull<RgTextureId *> sdr;
};

void setup_post_processing_passes(const PassCommonConfig &ccfg,
                                  const PostProcessingPassesConfig &cfg);

} // namespace ren
