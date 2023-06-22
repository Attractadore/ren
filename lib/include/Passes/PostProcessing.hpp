#pragma once
#include "RenderGraph.hpp"

namespace ren {

class TextureIDAllocator;
struct Pipelines;
struct PostProcessingOptions;

struct PostProcessingPassesConfig {
  RGTextureID texture;
  RGBufferID previous_exposure_buffer;
  const Pipelines *pipelines = nullptr;
  TextureIDAllocator *texture_allocator = nullptr;
  const PostProcessingOptions *options = nullptr;
};

struct PostProcessingPassesOutput {
  RGTextureID texture;
  RGBufferID exposure_buffer;
};

auto setup_post_processing_passes(Device &device, RenderGraph::Builder &rgb,
                                  const PostProcessingPassesConfig &cfg)
    -> PostProcessingPassesOutput;

} // namespace ren
