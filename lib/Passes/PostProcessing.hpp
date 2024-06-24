#pragma once
#include "Passes/Exposure.hpp"
#include "Support/NotNull.hpp"

namespace ren {

struct Pipelines;

struct PostProcessingPassesConfig {
  RgTextureId hdr;
  RgTextureId exposure;
  NotNull<RgTextureId *> sdr;
};

void setup_post_processing_passes(RgBuilder &rgb, NotNull<const Scene *> scene,
                                  const PostProcessingPassesConfig &cfg);

} // namespace ren
