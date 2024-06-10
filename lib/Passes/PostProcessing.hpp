#pragma once
#include "Passes/Exposure.hpp"
#include "Support/NotNull.hpp"

namespace ren {

struct Pipelines;

struct PostProcessingPassesConfig {
  RgTextureId hdr;
  RgTextureId exposure;
};

auto setup_post_processing_passes(RgBuilder &rgb, NotNull<const Scene *> scene,
                                  const PostProcessingPassesConfig &cfg)
    -> RgTextureId;

} // namespace ren
