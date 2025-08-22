#pragma once
#include "Spd.h"
#include "Std.h"

namespace ren::sh {

struct SsaoHiZArgs {
  DevicePtr<uint> spd_counter;
  uint num_mips;
  SH_ARRAY(Handle<RWTexture2D>, dsts, SPD_MAX_NUM_MIPS);
  Handle<Sampler2D> src;
  float znear;
};

} // namespace ren::sh
