#pragma once
#include "Passes/Exposure.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct Pipelines;
struct PostProcessingOptions;

struct PostProcessingPassesConfig {
  const Pipelines *pipelines = nullptr;
  const PostProcessingOptions *options = nullptr;
  RgTexture texture;
  ExposurePassOutput exposure;
};

struct PostProcessingPasses {
  RgPass reduce_luminance_histogram;
};

struct PostProcessingPassesOutput {
  PostProcessingPasses passes;
  RgTexture texture;
};

auto setup_post_processing_passes(RgBuilder &rgb,
                                  const PostProcessingPassesConfig &cfg)
    -> PostProcessingPassesOutput;

struct PostProcessingPassesData {
  const PostProcessingOptions *options = nullptr;
};

auto set_post_processing_passes_data(RenderGraph &rg,
                                     const PostProcessingPasses &passes,
                                     const PostProcessingPassesData &data)
    -> bool;

} // namespace ren
