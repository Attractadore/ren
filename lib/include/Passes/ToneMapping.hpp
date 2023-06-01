#pragma once
#include "RenderGraph.hpp"
#include "ToneMappingOptions.hpp"

namespace ren {

struct Pipelines;
class TextureIDAllocator;

struct ToneMappingPassConfig {
  RGTextureID texture;
  ToneMappingOptions options;
  TextureIDAllocator *texture_allocator;
  const Pipelines *pipelines;
};

struct ToneMappingPassOutput {
  RGTextureID texture;
};

auto setup_tone_mapping_pass(Device &device, RenderGraph::Builder &rgb,
                             const ToneMappingPassConfig &cfg)
    -> ToneMappingPassOutput;

} // namespace ren
