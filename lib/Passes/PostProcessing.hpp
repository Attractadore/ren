#pragma once
#include "Passes/Exposure.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Pipelines;

struct PostProcessingPassesConfig {
  const Pipelines *pipelines = nullptr;
  ExposureMode exposure_mode;
  glm::uvec2 viewport;
};

void setup_post_processing_passes(RgBuilder &rgb,
                                  const PostProcessingPassesConfig &cfg);

} // namespace ren
