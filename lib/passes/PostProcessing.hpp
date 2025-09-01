#pragma once
#include "Pass.hpp"

namespace ren {

struct PostProcessingPassesConfig {
  u64 frame_index = 0;
  RgTextureId hdr;
  NotNull<RgBufferId<float> *> exposure;
  NotNull<RgTextureId *> sdr;
};

void setup_post_processing_passes(const PassCommonConfig &ccfg,
                                  const PostProcessingPassesConfig &cfg);

} // namespace ren
