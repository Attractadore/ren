#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline vec2 hammersley(uint i, uint n) {
  uint x = i;
  uint y = bitfieldReverse(i) >> (31 - findLSB(n));
  return vec2(x, y) / float(n);
}

GLSL_NAMESPACE_END
