#ifndef REN_GLSL_ENCODE_H
#define REN_GLSL_ENCODE_H

#include "common.h"

GLSL_NAMESPACE_BEGIN

inline uint encode_float(float f, uint bits, float from, float to) {
  assert(from <= f && f <= to);
  return uint((f - from) / (to - from) * ((1u << bits) - 1));
}

inline uint encode_float_normalized(float f, uint bits) {
  return encode_float(f, bits, 0.0f, 1.0f);
}

inline float decode_float(uint value, uint bits, float from, float to) {
  return mix(from, to, float(value) / ((1u << bits) - 1));
}

inline float decode_float_normalized(uint value, uint bits) {
  return decode_float(value, bits, 0.0f, 1.0f);
}

const uint color_red_bits = 11;
const uint color_green_bits = 11;
const uint color_blue_bits = 10;
static_assert(color_red_bits + color_green_bits + color_blue_bits <= 32);

struct color_t {
  uint32_t color;
};

inline color_t encode_color(vec3 fcolor) {
  color_t color;
  color.color = encode_float_normalized(fcolor.r, color_red_bits);
  color.color <<= color_green_bits;
  color.color |= encode_float_normalized(fcolor.g, color_green_bits);
  color.color <<= color_blue_bits;
  color.color |= encode_float_normalized(fcolor.b, color_blue_bits);
  return color;
}

inline vec3 decode_color(color_t color) {
  uint blue = color.color & ((uint(1) << color_blue_bits) - 1);
  color.color >>= color_blue_bits;
  uint green = color.color & ((uint(1) << color_green_bits) - 1);
  color.color >>= color_green_bits;
  uint red = color.color & ((uint(1) << color_red_bits) - 1);
  return vec3(decode_float_normalized(red, color_red_bits),
              decode_float_normalized(green, color_green_bits),
              decode_float_normalized(blue, color_blue_bits));
}

struct normal_t {
  vec3 normal;
};

inline normal_t encode_normal(vec3 fnormal) {
  normal_t normal;
  normal.normal = fnormal;
  return normal;
}

inline vec3 decode_normal(normal_t normal) { return normal.normal; }

GLSL_NAMESPACE_END

#endif // REN_GLSL_ENCODE_H
