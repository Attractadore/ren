#pragma once
#include "LuminanceHistogram.h"
#include "Std.h"

namespace ren::sh {

struct ReduceLuminanceHistogramArgs {
  DevicePtr<LuminanceHistogram> histogram;
  DevicePtr<float> exposure;
  float exposure_compensation;
};

} // namespace ren::sh
