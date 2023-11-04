#pragma once
#include "Passes/Exposure.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Pipelines;
struct PostProcessingOptions;

struct PostProcessingPassesConfig {
  const Pipelines *pipelines = nullptr;
  const PostProcessingOptions *options = nullptr;
  glm::uvec2 viewport;
};

void setup_post_processing_passes(RgBuilder &rgb,
                                  const PostProcessingPassesConfig &cfg);

auto set_post_processing_passes_data(RenderGraph &rg,
                                     const PostProcessingOptions &opts) -> bool;

} // namespace ren
