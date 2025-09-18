#pragma once
#include "Std.h"

namespace ren::sh {

static const uint LTM_LLM_GROUP_SIZE_X = 8;
static const uint LTM_LLM_GROUP_SIZE_Y = 8;
static const uvec2 LTM_LLM_GROUP_SIZE =
    uvec2(LTM_LLM_GROUP_SIZE_X, LTM_LLM_GROUP_SIZE_Y);
static const uvec2 LTM_LLM_TILE_SIZE = LTM_LLM_GROUP_SIZE;

struct LocalToneMappingLLMArgs {
  Handle<Sampler2D> lightness;
  Handle<Sampler2D> accumulator;
  Handle<RWTexture2D> llm;
  uint mip;
};

} // namespace ren::sh
