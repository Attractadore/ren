#pragma once
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"

namespace ren {

class Device;
class TextureIDAllocator;

struct PostProcessingPassesConfig {
  RGTextureID texture;
  RGBufferID previous_exposure_buffer;
  PostProcessingOptions options;
  TextureIDAllocator *texture_allocator = nullptr;
  const Pipelines *pipelines = nullptr;
};

struct PostProcessingPassesOutput {
  RGTextureID texture;
  RGBufferID automatic_exposure_buffer;
};

auto setup_post_processing_passes(Device &device, RenderGraph::Builder &rgb,
                                  const PostProcessingPassesConfig &cfg)
    -> PostProcessingPassesOutput;

} // namespace ren
