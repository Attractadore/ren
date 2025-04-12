#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline float corput_base_2(uint i) {
  // Multiply by 2^-32.
  return bitfieldReverse(i) * uintBitsToFloat(0x2f800000);
}

inline float corput_base_3(uint i) {
  float r = 0.0f;
  float inv_base_n = 1.0f;
  for (uint k = 0; k <= 20; ++k) {
    if (i == 0) {
      break;
    }
    uint next = i / 3;
    uint digit = i - 3 * next;
    i = next;
    r = 3 * r + digit;
    inv_base_n /= 3.0f;
  }
  return r * inv_base_n;
}

inline vec2 hammersley_2d(uint i, uint n) {
  return vec2(i / float(n), corput_base_2(i));
}

inline vec3 hammersley_3d(uint i, uint n) {
  return vec3(i / float(n), corput_base_2(i), corput_base_3(i));
}

GLSL_NAMESPACE_END
