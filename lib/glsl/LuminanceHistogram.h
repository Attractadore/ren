#ifndef REN_GLSL_LUMINANCE_HISTOGRAM_H
#define REN_GLSL_LUMINANCE_HISTOGRAM_H

#include "BufferReference.h"
#include "Common.h"

GLSL_NAMESPACE_BEGIN

// Absolute threshold of vision is 1e-6 cd/m^2
const float MIN_LUMINANCE = 1.0e-7f;
// Eye damage is possible at 1e8 cd/m^2
const float MAX_LUMINANCE = 1.0e9f;

const float MIN_LOG_LUMINANCE = log2(MIN_LUMINANCE);
const float MAX_LOG_LUMINANCE = log2(MAX_LUMINANCE);

const uint NUM_LUMINANCE_HISTOGRAM_BINS = 64;

GLSL_REF_TYPE(4) LuminanceHistogramRef {
  uint bins[NUM_LUMINANCE_HISTOGRAM_BINS];
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_LUMINANCE_HISTOGRAM_H
