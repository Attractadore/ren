#ifndef REN_GLSL_POSTPROCESS_H
#define REN_GLSL_POSTPROCESS_H

#include "common.h"

REN_NAMESPACE_BEGIN

inline float get_luminance(vec3 color) {
  return dot(color, vec3(0.2126f, 0.7152f, 0.0722f));
}

REN_NAMESPACE_END

#endif // REN_GLSL_POSTPROCESS_H
