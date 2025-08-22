#pragma once
#include "Std.h"

namespace ren::sh {

struct BakeSpecularMapArgs {
  Handle<Sampler2D> equirectangular_map;
  Handle<RWTexture2DArray> specular_map;
  float roughness;
};

} // namespace ren::sh
