#ifndef REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H
#define REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H

#include "common.h"
#include "exposure.h"
#include "luminance_histogram.h"

REN_NAMESPACE_BEGIN

struct ReduceLuminanceHistogramConstants {
  REN_REFERENCE(LuminanceHistogram) histogram_ptr;
  REN_REFERENCE(Exposure) exposure_ptr;
  float exposure_compensation;
};

const uint REDUCE_LUMINANCE_HISTOGRAM_THREADS_X = NUM_LUMINANCE_HISTOGRAM_BINS;

REN_NAMESPACE_END

#endif // REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H
