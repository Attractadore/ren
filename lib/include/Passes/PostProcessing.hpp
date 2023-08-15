#pragma once
#include "Passes/Exposure.hpp"

namespace ren {

struct Pipelines;
struct PostProcessingOptions;

struct PostProcessingPassesConfig {
  const Pipelines *pipelines = nullptr;
  const PostProcessingOptions *options = nullptr;
};

void setup_post_processing_passes(RgBuilder &rgb,
                                  const PostProcessingPassesConfig &cfg);

auto set_post_processing_passes_data(RenderGraph &rg,
                                     const PostProcessingOptions &opts) -> bool;

} // namespace ren
