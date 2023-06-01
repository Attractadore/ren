#pragma once
#include "ToneMapping.hpp"

namespace ren {

struct ReinhardToneMappingPassConfig {
  RGTextureID texture;
  TextureIDAllocator *texture_allocator;
  const Pipelines *pipelines;
};

auto setup_reinhard_tone_mapping_pass(Device &Device, RenderGraph::Builder &rgb,
                                      const ReinhardToneMappingPassConfig &cfg)
    -> ToneMappingPassOutput;

} // namespace ren
