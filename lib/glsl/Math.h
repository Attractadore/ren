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

inline float acos_fast(float x) {
  float r = acos_0_to_1_fast(abs(x));
  return x < 0.0f ? PI - r : r;
}

// https://old.reddit.com/r/vulkan/comments/c4r7qx/erf_for_vulkan/esnvdnf/
inline float erf_fast(float x) {
  float xa = abs(x);

  float y = xa * (xa * (xa * 0.0038004543f + 0.020338153f) + 0.03533611f) +
            1.0000062f;

  // y = y^32
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;

  y = 1.0f - 1.0f / y;

  return x < 0.0f ? -y : y;
}

inline float erf_0_inf_fast(float x) {
  float y =
      x * (x * (x * 0.0038004543f + 0.020338153f) + 0.03533611f) + 1.0000062f;

  // y = y^32
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;
  y = y * y;

  y = 1.0f - 1.0f / y;

  return y;
}

GLSL_NAMESPACE_END
