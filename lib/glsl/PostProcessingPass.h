#ifndef REN_GLSL_POST_PROCESS_PASS_H
#define REN_GLSL_POST_PROCESS_PASS_H

#include "LuminanceHistogram.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

#define GLSL_POST_PROCESSING_CONSTANTS                                         \
  {                                                                            \
    GLSL_RESTRICT GLSL_BUFFER_REFERENCE(LuminanceHistogram) histogram;         \
    uint previous_exposure_texture;                                            \
    uint tex;                                                                  \
  }

const uint POST_PROCESSING_THREADS_X = 8;
const uint POST_PROCESSING_THREADS_Y = 8;

GLSL_NAMESPACE_END

#endif // REN_GLSL_POST_PROCESS_PASS_H
