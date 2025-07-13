#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

#define glsl_ceil_div(nom, denom) ((nom) / (denom) + uint((nom) % (denom) != 0))

inline uint ceil_div(uint nom, uint denom) { return glsl_ceil_div(nom, denom); }

// https://www.desmos.com/calculator/lzzhuthh1g
inline float acos_0_to_1_fast(float x) {
  x = clamp(x, 0.0f, 1.0f);
  float taylor_0 = 0.5f * PI - x - x * x * x / 6.0f;
  float taylor_1 = sqrt(2.0f * (1.0f - x)) * (1.0f + (1.0f - x) / 12.0f);
  return mix(taylor_0, taylor_1, x);
}

GLSL_NAMESPACE_END
