#ifndef REN_GLSL_POST_PROCESS_PASS_H
#define REN_GLSL_POST_PROCESS_PASS_H

#include "Exposure.h"
#include "LuminanceHistogram.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

#define GLSL_POST_PROCESSING_CONSTANTS                                         \
  {                                                                            \
    GLSL_RESTRICT GLSL_BUFFER_REFERENCE(LuminanceHistogram) histogram;         \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(Exposure)                \
        previous_exposure;                                                     \
    uint tex;                                                                  \
  }

const uint POST_PROCESSING_THREADS_X = 8;
const uint POST_PROCESSING_THREADS_Y = 8;
const uint POST_PROCESSING_WORK_SIZE_X = 2;
const uint POST_PROCESSING_WORK_SIZE_Y = 2;
static_assert(POST_PROCESSING_THREADS_X * POST_PROCESSING_THREADS_Y ==
              NUM_LUMINANCE_HISTOGRAM_BINS);

GLSL_NAMESPACE_END

#endif // REN_GLSL_POST_PROCESS_PASS_H
