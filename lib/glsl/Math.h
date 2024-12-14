#ifndef REN_GLSL_MATH_H
#define REN_GLSL_MATH_H

#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline uint ceil_div(uint nom, uint denom) {
  return nom / denom + uint(nom % denom != 0);
}

GLSL_NAMESPACE_END

#endif // REN_GLSL_MATH_H
