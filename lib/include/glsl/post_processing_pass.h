#ifndef REN_GLSL_POST_PROCESS_PASS_H
#define REN_GLSL_POST_PROCESS_PASS_H

#include "common.h"
#include "exposure.h"
#include "luminance_histogram.h"

REN_NAMESPACE_BEGIN

struct PostProcessingConstants {
  REN_REFERENCE(LuminanceHistogram) histogram_ptr;
  REN_REFERENCE(Exposure) previous_exposure_ptr;
  uint tex;
};

const uint POST_PROCESSING_THREADS_X = 8;
const uint POST_PROCESSING_THREADS_Y = 8;
const uint POST_PROCESSING_WORK_SIZE_X = 2;
const uint POST_PROCESSING_WORK_SIZE_Y = 2;
static_assert(POST_PROCESSING_THREADS_X * POST_PROCESSING_THREADS_Y ==
              NUM_LUMINANCE_HISTOGRAM_BINS);

REN_NAMESPACE_END

#endif // REN_GLSL_POST_PROCESS_PASS_H
