#pragma once
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

inline vec3 fresnel_f0(vec3 color, float metallic) {
  float ior = 1.5f;
  vec3 f0 = vec3((ior - 1.0f) / (ior + 1.0f));
  f0 = f0 * f0;
  return mix(f0, color, metallic);
}

// clang-format off
// G_2(l, v, h) = 1 / (1 + A(v) + A(l))
// A(s) = (-1 + sqrt(1 + 1/a(s)^2)) / 2
// a(s) = dot(n, s) / (alpha * sqrt(1 - dot(n, s)^2))
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

// https://math.stackexchange.com/a/1586015
inline vec3 uniform_sample_hemisphere(vec2 xy, vec3 n) {
  float phi = xy.x * TWO_PI;
  float z = xy.y;
  float r = sqrt(1.0f - z * z);
  vec3 d = vec3(r * cos(phi), r * sin(phi), z);

  vec3 t = normalize(ortho_vec(n));
  vec3 b = cross(n, t);

  return mat3(t, b, n) * d;
}

// https://cseweb.ucsd.edu/~viscomp/classes/cse168/sp21/lectures/168-lecture9.pdf
inline vec3 importance_sample_cosine_weighted_hemisphere(vec2 xi, vec3 n) {
  float phi = xi.x * TWO_PI;
  float cos_theta = sqrt(xi.y);
  float sin_theta = sqrt(1.0f - xi.y);
  vec3 d = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

  vec3 t = normalize(ortho_vec(n));
  vec3 b = cross(n, t);

  return mat3(t, b, n) * d;
}

inline vec3 importance_sample_lambertian(vec2 xy, vec3 n) {
  return importance_sample_cosine_weighted_hemisphere(xy, n);
}

inline vec3 lighting(vec3 n, vec3 l, vec3 v, vec3 albedo, vec3 f0,
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
  vec3 fresnel = f0 + (1.0f - f0) * pow(1.0f - lh, 5.0f);

  float smith = g_smith(roughness, nl, nv);

  // clang-format off
  // D(h) = alpha^2 / (pi * (1 + dot(n, h)^2 * (alpha^2 - 1))^2)
  // clang-format on
  float quot = 1.0f + nh2 * (alpha2 - 1.0f);
  float ggx_pi = alpha2 / (quot * quot);

  vec3 fs_nl_pi = (fresnel * smith * ggx_pi) / (4.0f * nv);
  vec3 fd_nl_pi = albedo * nl;

  vec3 L_o = float(nl > 0.0f) * (fd_nl_pi + fs_nl_pi) * illuminance / PI;

  return L_o;
}

inline vec3 ka_with_interreflection(float ka, vec3 albedo) {
  return ka * (1.0f - albedo * (1.0f - ka));
}

#if GL_core_profile

inline vec3 specular_occlusion(SampledTexture3D lut, vec3 f0, float roughness,
                               float nv, float ka) {
  float cosa = sqrt(1.0f - ka);
  vec2 ab = texture_lod(lut, vec3(roughness, nv, cosa), 0).xy;
  return f0 * ab.x + ab.y;
}

inline vec3 env_lighting(vec3 n, vec3 v, vec3 albedo, vec3 f0, float roughness,
                         vec3 luminance, float ka, SampledTexture3D so_lut) {
  vec3 kd = ka_with_interreflection(ka, albedo) * albedo;
  vec3 ks = specular_occlusion(so_lut, f0, roughness, dot(n, v), ka);
  return (kd + ks) * luminance;
}

inline vec3 env_lighting(vec3 n, vec3 v, vec3 albedo, vec3 f0, float roughness,
                         SampledTextureCube env_map, float ka,
                         SampledTexture3D so_lut) {
  vec3 kd = ka_with_interreflection(ka, albedo) * albedo;
  float nv = dot(n, v);
  vec3 ks = specular_occlusion(so_lut, f0, roughness, nv, ka);
  vec3 r = 2 * nv * n - v;
  int num_mips = texture_query_levels(env_map);
  float dlod = num_mips - 1;
  float slod = roughness * (num_mips - 2);
  return kd * texture_lod(env_map, n, dlod).rgb +
         ks * texture_lod(env_map, r, slod).rgb;
}

#endif // GL_core_profile

GLSL_NAMESPACE_END
