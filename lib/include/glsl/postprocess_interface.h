#ifndef REN_GLSL_POSTPROCESS_INTERFACE_H
#define REN_GLSL_POSTPROCESS_INTERFACE_H

#include "common.h"

REN_NAMESPACE_BEGIN

REN_BUFFER_REFERENCE(4) Exposure { float exposure; };

struct ReinhardPushConstants {
  REN_REFERENCE(Exposure) exposure_ptr;
  uint tex;
};

const uint REINHARD_THREADS_X = 8;
const uint REINHARD_THREADS_Y = 8;

REN_NAMESPACE_END

#endif // REN_GLSL_POSTPROCESS_INTERFACE_H
