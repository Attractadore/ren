#pragma once
#include "Pipeline.hpp"
#include "PipelineLoading.hpp"
#include "Postprocessing.hpp"
#include "RenderGraph.hpp"

namespace ren {

class Device;
class TextureIDAllocator;

struct PostprocessPassesConfig {
  RGTextureID texture;
  PostprocessingOptions options;
  TextureIDAllocator *texture_allocator;
  PostprocessingPipelines pipelines;
};

struct PostprocessPassesOutput {
  RGTextureID texture;
};

auto setup_postprocess_passes(Device &device, RenderGraph::Builder &rgb,
                              const PostprocessPassesConfig &cfg)
    -> PostprocessPassesOutput;

} // namespace ren
