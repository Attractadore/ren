#ifndef REN_GLSL_LIGHTING_H
#define REN_GLSL_LIGHTING_H

#include "DevicePtr.h"
#include "Std.h"
#include "Texture.h"
#include "Vertex.h"
#if GL_core_profile
#include "Texture.glsl"
#endif

GLSL_NAMESPACE_BEGIN

struct DirectionalLight {
  vec3 color;
  float illuminance;
  vec3 origin;
};

GLSL_DEFINE_PTR_TYPE(DirectionalLight, 4);

const float PI = 3.1415;

inline vec3 fresnel_f0(vec3 color, float metallic) {
  float ior = 1.5f;
  vec3 f0 = vec3((ior - 1.0f) / (ior + 1.0f));
  f0 = f0 * f0;
  return mix(f0, color, metallic);
}

// clang-format off
// G_2(l, v, h) = 1 / (1 + A(v) + A(l))
// A(s) = (-1 + sqrt(1 + 1/a(s)^2)) / 2
// a(s) = dot(n, s) / (alpha * sqrt(1 - dot(n, s)^2)
// A(s) = (-1 + sqrt(1 + alpha^2 * (1 - dot(n, s)^2) / dot(n, s)^2) / 2
// clang-format on
inline float g_smith(float roughness, float nl, float nv) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float nl2 = nl * nl;
  float nv2 = nv * nv;
  float lambda_l = sqrt(1.0f + alpha2 * (1.0f - nl2) / nl2);
  float lambda_v = sqrt(1.0f + alpha2 * (1.0f - nv2) / nv2);
  float G = 2.0f / (lambda_l + lambda_v);
  return G;
}

// GGX importance sampling function is given in "Microfacet Models for
// Refraction through Rough Surfaces":
// https://www.cs.cornell.edu/%7Esrm/publications/EGSR07-btdf.pdf
inline vec3 importance_sample_ggx(vec2 xy, float roughness, vec3 n) {
  float alpha = roughness * roughness;

  float cos_theta =
      sqrt((1.0f - xy.x) / (1.0f + (alpha * alpha - 1.0f) * xy.x));
  float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
  float phi = 2.0f * PI * xy.y;

  vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

  vec3 t = normalize(ortho_vec(n));
  vec3 b = cross(n, t);

  return mat3(t, b, n) * h;
}

inline vec2 hammersley(uint i, uint n) {
  uint x = i;
  uint y = bitfieldReverse(i) >> (31 - findLSB(n));
  return vec2(x, y) / float(n);
}

vec3 dhr(SampledTexture2D lut, vec3 f0, float roughness, float nv);

#if GL_core_profile
vec3 dhr(SampledTexture2D lut, vec3 f0, float roughness, float nv) {
  vec2 ab = texture_lod(lut, vec2(roughness, nv), 0).xy;
  return f0 * ab.x + ab.y;
}
#endif

inline vec3 lighting(vec3 n, vec3 l, vec3 v, vec3 color, float metallic,
                     float roughness, vec3 illuminance) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;

  vec3 h = normalize(v + l);

  float nl = dot(n, l);
  float nl2 = nl * nl;
  float nv = dot(n, v);
  float nv2 = nv * nv;
  float nh = dot(n, h);
  float nh2 = nh * nh;
  float lh = dot(l, h);

  // clang-format off
  // f_diff(l, v) = (1 - F(h, l)) * c / pi
  // f_spec(l, v) = F(h, l) * G_2(l, v, h) * D(h) / (4 * dot(n, l) * dot(n, v))
  // f(l, v) = f_diff(l, v) + f_spec(l, v)
  // L_o = f(l, v) * E_p * dot(n, l)
  // clang-format on

  // F(h, l) = F_0 + (1 - F_0) * (1 - dot(h, l))^5
  vec3 f0 = fresnel_f0(color, metallic);
  vec3 fresnel = f0 + (1.0f - f0) * pow(1.0f - lh, 5.0f);

  float smith = g_smith(roughness, nl, nv);

  // clang-format off
  // D(h) = alpha^2 / (pi * (1 + dot(n, h)^2 * (alpha^2 - 1))^2)
  // clang-format on
  float quot = 1.0f + nh2 * (alpha2 - 1.0f);
  float ggx_pi = alpha2 / (quot * quot);

  vec3 fs_nl_pi = (fresnel * smith * ggx_pi) / (4.0f * nv);
  vec3 fd_nl_pi = mix(color, vec3(0.0f), metallic) * nl;

  vec3 L_o = float(nl > 0.0f) * (fd_nl_pi + fs_nl_pi) * illuminance / PI;

  return L_o;
}

inline vec3 const_env_lighting(vec3 n, vec3 v, vec3 color, float metallic,
                               float roughness, vec3 luminance,
                               SampledTexture2D dhr_lut) {

  vec3 kd = mix(color, vec3(0.0f), metallic);

  // Use the split integral approximation:
  // clang-format off
  // \int f_spec L dot(l, n) dl = \int D(r) L dot(n, l) dl * \int f_spec dot(l, n) dl = L * R(v)
  // clang-format on
  vec3 f0 = fresnel_f0(color, metallic);
  vec3 ks = dhr(dhr_lut, f0, roughness, dot(n, v));

  vec3 L_o = (kd + ks) * luminance;

  return L_o;
}

GLSL_NAMESPACE_END

#endif // REN_GLSL_LIGHTING_H
