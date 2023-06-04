#ifndef REN_GLSL_LUMINANCE_HISTOGRAM_H
#define REN_GLSL_LUMINANCE_HISTOGRAM_H

#include "common.h"

REN_NAMESPACE_BEGIN

// Absolute threshold of vision is 1e-6 cd/m^2
const float MIN_LUMINANCE = 1.0e-7f;
// Eye damage is possible at 1e8 cd/m^2
const float MAX_LUMINANCE = 1.0e9f;

const float MIN_LOG_LUMINANCE = log2(MIN_LUMINANCE);
const float MAX_LOG_LUMINANCE = log2(MAX_LUMINANCE);

const uint NUM_LUMINANCE_HISTOGRAM_BINS = 64;

REN_BUFFER_REFERENCE(4) LuminanceHistogram {
  uint bins[NUM_LUMINANCE_HISTOGRAM_BINS];
};

REN_NAMESPACE_END

#endif // REN_GLSL_LUMINANCE_HISTOGRAM_H
