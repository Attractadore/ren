#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

inline uint encode_float(float f, uint bits, float from = 0.0f, float to = 1.0f) {
  assert(from <= f && f <= to);
  return (f - from) / (to - from) * ((uint(1) << bits) - 1);
}

inline float decode_float(uint value, uint bits, float from = 0.0f, float to = 1.0f) {
  float alpha = float(value) / ((uint(1) << bits) - 1);
  return lerp(from, to, float(value) / ((uint(1) << bits) - 1));
}

constexpr uint color_red_bits = 11;
constexpr uint color_green_bits = 11;
constexpr uint color_blue_bits = 10;
static_assert(color_red_bits + color_green_bits + color_blue_bits <= 32);

typedef uint32_t color_t;

inline color_t encode_color(float3 fcolor) {
  uint color = encode_float(fcolor.r, color_red_bits);
  color <<= color_green_bits;
  color |= encode_float(fcolor.g, color_green_bits);
  color <<= color_blue_bits;
  color |= encode_float(fcolor.b, color_blue_bits);
  return color;
}

inline float3 decode_color(color_t color) {
  uint blue = color & ((uint(1) << color_blue_bits) - 1);
  color >>= color_blue_bits;
  uint green = color & ((uint(1) << color_green_bits) - 1);
  color >>= color_green_bits;
  uint red = color & ((uint(1) << color_red_bits) - 1);
  return float3(decode_float(red, color_red_bits),
                decode_float(green, color_green_bits),
                decode_float(blue, color_blue_bits));
}

REN_NAMESPACE_END
