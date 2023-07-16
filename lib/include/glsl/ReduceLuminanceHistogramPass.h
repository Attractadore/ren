#ifndef REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H
#define REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H

#include "Exposure.h"
#include "LuminanceHistogram.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

#define GLSL_REDUCE_LUMINANCE_HISTOGRAM_CONSTANTS                              \
  {                                                                            \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(LuminanceHistogram)      \
        histogram;                                                             \
    GLSL_RESTRICT GLSL_WRITEONLY GLSL_BUFFER_REFERENCE(Exposure) exposure;     \
    float exposure_compensation;                                               \
  }

const uint REDUCE_LUMINANCE_HISTOGRAM_THREADS_X = NUM_LUMINANCE_HISTOGRAM_BINS;

GLSL_NAMESPACE_END

#endif // REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H
