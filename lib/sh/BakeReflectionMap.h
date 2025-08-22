#pragma once
#include "Std.h"

namespace ren::sh {

struct BakeReflectionMapArgs {
  Handle<Sampler2D> equirectangular_map;
  Handle<RWTexture2DArray> reflectance_map;
};

} // namespace ren::sh
