#pragma once
#include "Std.h"

namespace ren::sh {

// Absolute threshold of vision is 1e-6 cd/m^2
static const float MIN_LUMINANCE = 1.0e-7f;
// Eye damage is possible at 1e8 cd/m^2
static const float MAX_LUMINANCE = 1.0e9f;

static const float MIN_LOG_LUMINANCE = log2(MIN_LUMINANCE);
static const float MAX_LOG_LUMINANCE = log2(MAX_LUMINANCE);

static const uint NUM_LUMINANCE_HISTOGRAM_BINS = 64;

struct LuminanceHistogram {
  uint bins[NUM_LUMINANCE_HISTOGRAM_BINS];
};

} // namespace ren::sh
