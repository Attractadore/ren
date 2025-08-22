#pragma once
#include "Std.h"

namespace ren::sh {

struct BakeIrradianceMapArgs {
  Handle<Sampler2D> equirectangular_map;
  Handle<RWTexture2DArray> irradiance_map;
};

} // namespace ren::sh
