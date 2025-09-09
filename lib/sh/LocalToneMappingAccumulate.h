#pragma once
#include "Std.h"

namespace ren::sh {

struct LocalToneMappingAccumulateArgs {
  Handle<Sampler2D> lightness;
  Handle<Sampler2D> weights;
  Handle<Sampler2D> src_accumulator;
  Handle<RWTexture2D> dst_accumulator;
  uint mip;
  int contrast_boost;
};

} // namespace ren::sh
