#pragma once
#include "Std.h"

namespace ren::sh {

static const uint LTM_ACCUMULATE_GROUP_SIZE_X = 16;
static const uint LTM_ACCUMULATE_GROUP_SIZE_Y = 16;
static const uvec2 LTM_ACCUMULATE_GROUP_SIZE =
    uvec2(LTM_ACCUMULATE_GROUP_SIZE_X, LTM_ACCUMULATE_GROUP_SIZE_Y);
static const uint LTM_ACCUMULATE_UNROLL_X = 4;
static const uint LTM_ACCUMULATE_UNROLL_Y = 4;
static const uvec2 LTM_ACCUMULATE_UNROLL =
    uvec2(LTM_ACCUMULATE_UNROLL_X, LTM_ACCUMULATE_UNROLL_Y);
static const uvec2 LTM_ACCUMULATE_TILE_SIZE =
    LTM_ACCUMULATE_GROUP_SIZE * LTM_ACCUMULATE_UNROLL;

struct LocalToneMappingAccumulateArgs {
  Handle<Sampler2D> lightness;
  Handle<Sampler2D> weights;
  Handle<Sampler2D> src_accumulator;
  Handle<RWTexture2D> dst_accumulator;
  uint dst_mip;
  uint llm_mip;
  int contrast_boost;
};

} // namespace ren::sh
