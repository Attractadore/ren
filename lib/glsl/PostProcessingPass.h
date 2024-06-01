#ifndef REN_GLSL_POST_PROCESS_PASS_H
#define REN_GLSL_POST_PROCESS_PASS_H

#include "Common.h"
#include "LuminanceHistogram.h"

GLSL_NAMESPACE_BEGIN

struct PostProcessingPassConstants {
  GLSL_REF(LuminanceHistogramRef) histogram;
  uint previous_exposure_texture;
  uint hdr_texture;
  uint sdr_texture;
};

const uint POST_PROCESSING_THREADS_X = 8;
const uint POST_PROCESSING_THREADS_Y = 8;

GLSL_NAMESPACE_END

#endif // REN_GLSL_POST_PROCESS_PASS_H
