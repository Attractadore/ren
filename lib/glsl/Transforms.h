#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline mat3 adjugate(mat3 m) {
  return mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
}

inline mat3 normal(mat3 m) { return adjugate(m); }

inline vec2 pixel_view_space_size(float rcp_p00, float rcp_p11, vec2 rcp_size,
                                  float z) {
  // s_ndc = a * s_v / -z_v =>
  // s_v = s_ndc * -z_v / a
  // s_ndc = 2 * s_uv = 2 / size
  return (2.0f * -z) * vec2(rcp_p00, rcp_p11) * rcp_size;
}

inline vec3 normal_offset(vec3 p, vec3 v, vec3 n, vec2 pixel_size) {
  float diag = length(pixel_size);
  float cos_v = dot(v, n);
  float sin_v = sqrt(1.0f - cos_v * cos_v);
  return p + (0.5f * sin_v * diag) * n;
}

inline vec2 ndc_to_uv(vec2 ndc) {
  return vec2(0.5f + 0.5f * ndc.x, 0.5f - 0.5f * ndc.y);
}

inline vec2 uv_to_ndc(vec2 uv) {
  return vec2(2.0f * uv.x - 1.0f, 1.0f - 2.0f * uv.y);
}

inline vec3 view_to_ndc(float p00, float p11, float znear, vec3 p) {
  return vec3(p.x * p00, p.y * p11, znear) / -p.z;
}

inline vec3 ndc_to_view(float rcp_p00, float rcp_p11, float znear, vec3 p) {
  float z = -znear / p.z;
  return vec3(p.x * rcp_p00 * -z, p.y * rcp_p11 * -z, z);
}

inline uvec2 linear_to_morton_2d(uint i) {
  uint x = i & 0x55555555;
  uint y = (i >> 1) & 0x55555555;
  // All bits in x and  y need to be shifted by a combination of shifts of
  // lengths 8, 4, 2 and 1.
  // Bit i needs to be shifted by i / 2.
  uvec2 m = uvec2(x, y);
  m = (m | (m >> uvec2(1))) & uvec2(0x33333333);
  m = (m | (m >> uvec2(2))) & uvec2(0x0F0F0F0F);
  m = (m | (m >> uvec2(4))) & uvec2(0x00FF00FF);
  m = (m | (m >> uvec2(8))) & uvec2(0x0000FFFF);
  return m;
}

inline float pack_depth_linear_16bit(float d, float znear) {
  return znear * (1.0f - d) / d;
}

inline float pack_z_linear_16bit(float z, float znear) { return z - znear; }

inline float unpack_z_linear_16bit(float z, float znear) { return z + znear; }

GLSL_NAMESPACE_END
