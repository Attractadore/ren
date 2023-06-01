#pragma once
#include "PipelineLoading.hpp"
#include "PostprocessingOptions.hpp"
#include "RenderGraph.hpp"

namespace ren {

class Device;
class TextureIDAllocator;

struct PostprocessingPassesConfig {
  RGTextureID texture;
  PostprocessingOptions options;
  TextureIDAllocator *texture_allocator;
  const Pipelines *pipelines;
};

struct PostprocessingPassesOutput {
  RGTextureID texture;
};

auto setup_postprocessing_passes(Device &device, RenderGraph::Builder &rgb,
                                 const PostprocessingPassesConfig &cfg)
    -> PostprocessingPassesOutput;

} // namespace ren
