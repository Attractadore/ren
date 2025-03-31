#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline uint ceil_div(uint nom, uint denom) {
  return nom / denom + uint(nom % denom != 0);
}

GLSL_NAMESPACE_END
