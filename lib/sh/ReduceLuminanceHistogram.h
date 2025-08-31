#pragma once
#include "LuminanceHistogram.h"
#include "Std.h"

namespace ren::sh {

struct ReduceLuminanceHistogramArgs {
  DevicePtr<LuminanceHistogram> histogram;
  DevicePtr<float> exposure;
  float exposure_compensation;
  float dark_adaptation_time;
  float bright_adaptation_time;
  float dt;
};

} // namespace ren::sh
