#pragma once
#include "Std.h"
#include "Transforms.h"

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
DIFFERENTIABLE
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

DIFFERENTIABLE
inline float D_ggx(float roughness, float NoH) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float q = 1 + NoH * NoH * (alpha2 - 1);
  return alpha2 / (PI * q * q);
}

// GGX importance sampling function is given in "Microfacet Models for
// Refraction through Rough Surfaces":
// https://www.cs.cornell.edu/%7Esrm/publications/EGSR07-btdf.pdf
inline vec3 importance_sample_ggx(vec2 Xi, float roughness) {
  float alpha = roughness * roughness;
  float z = sqrt((1.0f - Xi.x) / (1.0f + (alpha * alpha - 1.0f) * Xi.x));
  z = min(z, 1.0f);
  float r = sqrt(1.0f - z * z);
  float phi = TWO_PI * Xi.y;
  return vec3(r * cos(phi), r * sin(phi), z);
}

inline vec3 importance_sample_ggx(vec2 Xi, float roughness, vec3 N) {
  vec3 H = importance_sample_ggx(Xi, roughness);
  vec3 T = normalize(ortho_vec(N));
  vec3 B = cross(N, T);
  return mat3(T, B, N) * H;
}

#if __cplusplus

DIFFERENTIABLE
inline double F_schlick(double f0, double NoV) {
  return f0 + (1 - f0) * pow(1 - NoV, 5);
}

DIFFERENTIABLE
inline dvec3 F_schlick(dvec3 f0, double NoV) {
  return f0 + (1.0 - f0) * pow(1 - NoV, 5);
}

DIFFERENTIABLE
inline double G_smith(double roughness, double NoL, double NoV) {
  double alpha = roughness * roughness;
  double alpha2 = alpha * alpha;
  double NoL2 = NoL * NoL;
  double NoV2 = NoV * NoV;
  double lambda_L = sqrt(1.0 + alpha2 * (1.0 - NoL2) / NoL2);
  double lambda_V = sqrt(1.0 + alpha2 * (1.0 - NoV2) / NoV2);
  return 2.0 / (lambda_L + lambda_V);
}

DIFFERENTIABLE
inline double D_ggx(double roughness, double NoH) {
  double alpha = roughness * roughness;
  double alpha2 = alpha * alpha;
  double q = 1 + NoH * NoH * (alpha2 - 1);
  return alpha2 / (3.1416 * q * q);
}

inline dvec3 importance_sample_ggx(dvec2 Xi, double roughness) {
  double alpha = roughness * roughness;
  double z = sqrt((1 - Xi.x) / (1 + (alpha * alpha - 1) * Xi.x));
  double r = sqrt(1 - z * z);
  double phi = 6.2832 * Xi.y;
  return dvec3(r * cos(phi), r * sin(phi), z);
}

#endif

GLSL_NAMESPACE_END
