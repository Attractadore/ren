#pragma once
#include "Std.h"

namespace ren::sh {

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

// https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
inline float r1_seq(float i, float seed) {
  const float g = 1.6180339887498948482;
  const float a1 = 1.0f / g;
  return fract(seed + a1 * i);
}

inline float r1_seq(float i) { return r1_seq(i, 0.5f); }

inline vec2 r2_seq(float i, vec2 seed) {
  const float g = 1.32471795724474602596f;
  const float a1 = 1.0f / g;
  const float a2 = a1 * a1;
  return fract(seed + vec2(a1, a2) * i);
}

inline vec2 r2_seq(float i) { return r2_seq(i, vec2(0.5f)); }

inline vec3 r3_seq(float i, vec3 seed) {
  const float g = 1.22074408460575947536;
  const float a1 = 1.0f / g;
  const float a2 = a1 * a1;
  const float a3 = a2 * a1;
  return fract(seed + vec3(a1, a2, a3) * i);
}

inline vec3 r3_seq(float i) { return r3_seq(i, vec3(0.5f)); }

} // namespace ren::sh
