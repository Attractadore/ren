#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline vec3 F_schlick_f0(vec3 color, float metallic) {
  float ior = 1.5f;
  vec3 f0 = vec3((ior - 1.0f) / (ior + 1.0f));
  f0 = f0 * f0;
  return mix(f0, color, metallic);
}

DIFFERENTIABLE
inline float F_schlick(float f0, float NoV) {
  return f0 + (1.0f - f0) * pow(1.0f - NoV, 5.0f);
}

DIFFERENTIABLE
inline vec3 F_schlick(vec3 f0, float NoV) {
  return f0 + (1.0f - f0) * pow(1.0f - NoV, 5.0f);
}

// clang-format off
// G_2(l, v, h) = 1 / (1 + A(v) + A(l))
// A(s) = (-1 + sqrt(1 + 1/a(s)^2)) / 2
// a(s) = dot(n, s) / (alpha * sqrt(1 - dot(n, s)^2))
// A(s) = (-1 + sqrt(1 + alpha^2 * (1 - dot(n, s)^2) / dot(n, s)^2) / 2
// clang-format on
inline float G_smith(float roughness, float nl, float nv) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float nl2 = nl * nl;
  float nv2 = nv * nv;
  float lambda_l = sqrt(1.0f + alpha2 * (1.0f - nl2) / nl2);
  float lambda_v = sqrt(1.0f + alpha2 * (1.0f - nv2) / nv2);
  float G = 2.0f / (lambda_l + lambda_v);
  return G;
}

inline float D_ggx(float roughness, float NoH) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float q = 1 + NoH * NoH * (alpha2 - 1);
  return alpha2 / (PI * q * q);
}

GLSL_NAMESPACE_END
