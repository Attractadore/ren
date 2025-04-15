#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline mat3 adjugate(mat3 m) {
  return mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
}

inline mat3 normal(mat3 m) { return adjugate(m); }

inline vec2 pixel_view_space_size(float p00, float p11, vec2 size, float z) {
  // s_ndc = a * s_v / z_v =>
  // s_v = s_ndc * z_v / a
  // s_ndc = 2 * s_uv = 2 / size
  return (2.0f * z) / (vec2(p00, p11) * size);
}

inline vec3 normal_offset(vec3 p, vec3 v, vec3 n, vec2 pixel_size) {
  float diag = length(pixel_size);
  float sin_v = dot(v, n);
  return p + (0.5f * sin_v * diag) * n;
}

GLSL_NAMESPACE_END
