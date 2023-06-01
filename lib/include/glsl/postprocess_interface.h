#ifndef REN_GLSL_POSTPROCESS_INTERFACE_H
#define REN_GLSL_POSTPROCESS_INTERFACE_H

#include "common.h"
#include "exposure.h"

REN_NAMESPACE_BEGIN

const float MIN_LUMINANCE = 1.0f / 65536.0f;
const float MIN_LOG_LUMINANCE = log2(MIN_LUMINANCE);
const float MAX_LOG_LUMINANCE = 16.0f;
const uint NUM_LUMINANCE_HISTOGRAM_BINS = 64;

REN_BUFFER_REFERENCE(4) LuminanceHistogram {
  uint bins[NUM_LUMINANCE_HISTOGRAM_BINS];
};

struct BuildLuminanceHistogramConstants {
  REN_REFERENCE(LuminanceHistogram) histogram_ptr;
  uint tex;
};

const uint BUILD_LUMINANCE_HISTOGRAM_THREADS_X = 8;
const uint BUILD_LUMINANCE_HISTOGRAM_THREADS_Y = 8;
const uint BUILD_LUMINANCE_HISTOGRAM_ITEMS_X = 4;
const uint BUILD_LUMINANCE_HISTOGRAM_ITEMS_Y = 4;
static_assert(NUM_LUMINANCE_HISTOGRAM_BINS ==
              BUILD_LUMINANCE_HISTOGRAM_THREADS_X *
                  BUILD_LUMINANCE_HISTOGRAM_THREADS_Y);

struct ReduceLuminanceHistogramConstants {
  REN_REFERENCE(LuminanceHistogram) histogram_ptr;
  REN_REFERENCE(Exposure) previous_exposure_ptr;
  REN_REFERENCE(Exposure) exposure_ptr;
  float exposure_compensation;
};

const uint REDUCE_LUMINANCE_HISTOGRAM_THREADS_X = NUM_LUMINANCE_HISTOGRAM_BINS;

struct ReinhardConstants {
  uint tex;
};

const uint REINHARD_THREADS_X = 16;
const uint REINHARD_THREADS_Y = 16;

REN_NAMESPACE_END

#endif // REN_GLSL_POSTPROCESS_INTERFACE_H
