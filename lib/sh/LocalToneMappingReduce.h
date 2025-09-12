#pragma once
#include "Std.h"

// https://bartwronski.com/2022/03/07/fast-gpu-friendly-antialiasing-downsampling-filter/
// https://www.shadertoy.com/view/fsjBWm

namespace ren::sh {

static const uint LTM_REDUCE_GROUP_SIZE_X = 16;
static const uint LTM_REDUCE_GROUP_SIZE_Y = 16;
static const uvec2 LTM_REDUCE_GROUP_SIZE =
    uvec2(LTM_REDUCE_GROUP_SIZE_X, LTM_REDUCE_GROUP_SIZE_Y);
static const uint LTM_REDUCE_UNROLL_X = 4;
static const uint LTM_REDUCE_UNROLL_Y = 4;
static const uvec2 LTM_REDUCE_UNROLL =
    uvec2(LTM_REDUCE_UNROLL_X, LTM_REDUCE_UNROLL_Y);
static const uvec2 LTM_REDUCE_TILE_SIZE =
    LTM_REDUCE_GROUP_SIZE * LTM_REDUCE_UNROLL;

struct LocalToneMappingReduceArgs {
  Handle<Sampler2D> src_lightness;
  Handle<Sampler2D> src_weights;
  Handle<RWTexture2D> dst_lightness;
  Handle<RWTexture2D> dst_weights;
  uint src_mip;
};

} // namespace ren::sh
