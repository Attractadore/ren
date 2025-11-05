#pragma once
#include "Std.h"
#include "Transforms.h"

namespace ren::sh {

struct Material {
  vec4 base_color;
  Handle<Sampler2D> base_color_texture;
  float occlusion_strength;
  float roughness;
  float metallic;
  Handle<Sampler2D> orm_texture;
  float normal_scale;
  Handle<Sampler2D> normal_texture;
};

struct DirectionalLight {
  vec3 color;
  float illuminance;
  vec3 origin;
};

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

  vec3 t = normalize(make_orthogonal_vector(n));
  vec3 b = cross(n, t);

  return mat3(t, b, n) * h;
}

// https://math.stackexchange.com/a/1586015
inline vec3 uniform_sample_hemisphere(vec2 xy, vec3 n) {
  float phi = xy.x * TWO_PI;
  float z = xy.y;
  float r = sqrt(1.0f - z * z);
  vec3 d = vec3(r * cos(phi), r * sin(phi), z);

  vec3 t = normalize(make_orthogonal_vector(n));
  vec3 b = cross(n, t);

  return mat3(t, b, n) * d;
}

// https://cseweb.ucsd.edu/~viscomp/classes/cse168/sp21/lectures/168-lecture9.pdf
inline vec3 importance_sample_cosine_weighted_hemisphere(vec2 xi, vec3 n) {
  float phi = xi.x * TWO_PI;
  float cos_theta = sqrt(xi.y);
  float sin_theta = sqrt(1.0f - xi.y);
  vec3 d = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

  vec3 t = normalize(make_orthogonal_vector(n));
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
  float nv = dot(n, v);
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

// https://c0de517e.blogspot.com/2016/07/siggraph-2015-notes-for-approximate.html
inline vec3 directional_albedo(vec3 f0, float roughness, float NoV) {
  float bias = exp2(-7.0f * NoV - 4.0f * roughness * roughness);
  float scale = 1.0f - bias -
                roughness * roughness *
                    max(bias, min(roughness, 0.739f + 0.323f * NoV) - 0.434f);
  return f0 * scale + bias;
}

// https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/Filament/ambient_occlusion.va.fs#L24
inline float specular_occlusion(vec3 R, float roughness, vec3 C, float ka) {
  float cos_vis = sqrt(1.0f - ka);
  float cos_ndf = exp2(-3.321928f * roughness * roughness);

  float r_vis = acos_0_to_1_fast(cos_vis);
  float r_ndf = acos_0_to_1_fast(cos_ndf);
  float d = acos_fast(dot(R, C));

  float intersection_area = 0.0f;
  if (min(r_vis, r_ndf) <= max(r_vis, r_ndf) - d) {
    intersection_area = 1.0f - max(cos_vis, cos_ndf);
  } else if (r_vis + r_vis <= d) {
    intersection_area = 0.0f;
  } else {
    float delta = abs(r_vis - r_ndf);
    float x = 1.0 - clamp((d - delta) / max(r_vis + r_ndf - delta, 1e-4f), 0.0f,
                          1.0f);
    float area = x * x * (-2.0 * x + 3.0);
    intersection_area = area * (1.0 - max(cos_vis, cos_ndf));
  }
  float ndf_area = 1.0f - cos_ndf;

  float so = clamp(intersection_area / ndf_area, 0.0f, 1.0f);

  return mix(1.0f, so, smoothstep(0.01f, 0.09f, roughness));
}

inline vec3 env_lighting(vec3 N, vec3 V, vec3 albedo, vec3 f0, float roughness,
                         vec3 luminance, float ka, vec3 bN) {
  float NoV = dot(N, V);
  vec3 R = reflect(-V, N);
  vec3 kd = ka_with_interreflection(ka, albedo) * albedo;
  vec3 ks = specular_occlusion(R, roughness, bN, ka) *
            directional_albedo(f0, roughness, NoV);
  return (kd + ks) * luminance;
}

#if __SLANG__

inline vec3 env_lighting(vec3 N, vec3 V, vec3 albedo, vec3 f0, float roughness,
                         SamplerCube env_map, float ka, vec3 bN) {
  float NoV = dot(N, V);
  vec3 R = reflect(-V, N);
  vec3 kd = ka_with_interreflection(ka, albedo) * albedo;
  vec3 ks = specular_occlusion(R, roughness, bN, ka) *
            directional_albedo(f0, roughness, NoV);
  uint num_mips = TextureMips(env_map);
  float dlod = num_mips - 1;
  float slod = roughness * (num_mips - 2);
  return kd * env_map.SampleLevel(N, dlod).rgb +
         ks * env_map.SampleLevel(R, slod).rgb;
}

#endif

} // namespace ren::sh
