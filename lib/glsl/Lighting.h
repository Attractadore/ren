#ifndef REN_GLSL_LIGHTING_H
#define REN_GLSL_LIGHTING_H

#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

struct DirectionalLight {
  vec3 color;
  float illuminance;
  vec3 origin;
};

GLSL_DEFINE_PTR_TYPE(DirectionalLight, 4);

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
  float ior = 1.5f;
  vec3 f0 = vec3((ior - 1.0f) / (ior + 1.0f));
  f0 = f0 * f0;
  f0 = mix(f0, color, metallic);
  vec3 fresnel = f0 + (1.0f - f0) * pow(1.0f - lh, 5.0f);

  // clang-format off
  // G_2(l, v, h) = 1 / (1 + A(v) + A(l))
  // A(s) = (-1 + sqrt(1 + 1/a(s)^2)) / 2
  // a(s) = dot(n, s) / (alpha * sqrt(1 - dot(n, s)^2)
  // A(s) = (-1 + sqrt(1 + alpha^2 * (1 - dot(n, s)^2) / dot(n, s)^2) / 2
  // clang-format on
  float lambda_l = sqrt(1.0f + alpha2 * (1.0f - nl2) / nl2);
  float lambda_v = sqrt(1.0f + alpha2 * (1.0f - nv2) / nv2);
  float half_smith = 1.0f / (lambda_l + lambda_v);

  // clang-format off
  // D(h) = alpha^2 / (pi * (1 + dot(n, h)^2 * (alpha^2 - 1))^2)
  // clang-format on
  float quot = 1.0f + nh2 * (alpha2 - 1.0f);
  float ggx_pi = alpha2 / (quot * quot);

  vec3 fs_nl_pi = (fresnel * half_smith * ggx_pi) / (2.0f * nv);
  vec3 fd_nl_pi = (1.0f - fresnel) * mix(color, vec3(0.0f), metallic) * nl;

  const float PI = 3.1415f;

  vec3 L_o = float(nl > 0.0f) * (fd_nl_pi + fs_nl_pi) * illuminance / PI;

  return L_o;
}

GLSL_NAMESPACE_END

#endif // REN_GLSL_LIGHTING_H
