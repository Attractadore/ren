#pragma once
#include "Std.h"

namespace ren::sh {

static const uint LTM_ACCUMULATE_GROUP_SIZE_X = 8;
static const uint LTM_ACCUMULATE_GROUP_SIZE_Y = 8;
static const uvec2 LTM_ACCUMULATE_GROUP_SIZE =
    uvec2(LTM_ACCUMULATE_GROUP_SIZE_X, LTM_ACCUMULATE_GROUP_SIZE_Y);
static const uint LTM_ACCUMULATE_UNROLL_X = 2;
static const uint LTM_ACCUMULATE_UNROLL_Y = 2;
static const uvec2 LTM_ACCUMULATE_UNROLL =
    uvec2(LTM_ACCUMULATE_UNROLL_X, LTM_ACCUMULATE_UNROLL_Y);
static const uvec2 LTM_ACCUMULATE_TILE_SIZE =
    LTM_ACCUMULATE_GROUP_SIZE * LTM_ACCUMULATE_UNROLL;

struct LocalToneMappingAccumulateArgs {
  Handle<RWTexture2D> src_lightness;
  Handle<RWTexture2D> src_weights;
  Handle<RWTexture2D> src_accumulator;
  Handle<RWTexture2D> dst_lightness;
  Handle<RWTexture2D> dst_weights;
  Handle<RWTexture2D> dst_accumulator;
  int contrast_boost;
  uint y_offset;
  uint y_size;
};

} // namespace ren::sh
