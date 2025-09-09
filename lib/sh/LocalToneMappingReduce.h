#pragma once
#include "Std.h"

namespace ren::sh {

static const uint LTM_REDUCE_GROUP_SIZE_X = 8;
static const uint LTM_REDUCE_GROUP_SIZE_Y = 8;
static const uvec2 LTM_REDUCE_GROUP_SIZE =
    uvec2(LTM_REDUCE_GROUP_SIZE_X, LTM_REDUCE_GROUP_SIZE_Y);
static const uvec2 LTM_REDUCE_TILE_SIZE = LTM_REDUCE_GROUP_SIZE;

struct LocalToneMappingReduceArgs {
  Handle<Sampler2D> src_lightness;
  Handle<Sampler2D> src_weights;
  Handle<RWTexture2D> dst_lightness;
  Handle<RWTexture2D> dst_weights;
  uint src_mip;
};

} // namespace ren::sh
