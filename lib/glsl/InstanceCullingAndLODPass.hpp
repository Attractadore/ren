#pragma once
#include "InstanceCullingAndLODPass.h"

namespace ren {

struct InstanceCullingAndLODSettings {
  u32 feature_mask = 0;
  float lod_triangle_pixel_count = 0.0f;
  i32 lod_bias = 0;
};

} // namespace ren
