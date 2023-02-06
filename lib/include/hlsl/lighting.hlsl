#pragma once
#include "lighting.h"

inline float3 lighting(float3 n, float3 l, float3 v, float3 color,
                       float metallic, float roughness, float3 illuminance) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;

  float3 h = normalize(v + l);

  float nl = dot(n, l);
  float nl2 = nl * nl;
  float nv = dot(n, v);
  float nv2 = nv * nv;
  float nh = dot(n, h);
  float nh2 = nh * nh;
  float lh = dot(l, h);

  float3 fd = lerp(color, 0.0f, metallic);

  float ior = 1.5f;
  float3 f0 = (ior - 1.0f) / (ior + 1.0f);
  f0 = f0 * f0;
  f0 = lerp(f0, color, metallic);

  float3 frenel = f0 + (1.0f - f0) * pow(1.0f - lh, 5.0f);

  float lambda_l = sqrt(1.0f + alpha2 * (1.0f - nl2) / nl2);
  float lambda_v = sqrt(1.0f + alpha2 * (1.0f - nv2) / nv2);

  float smith = 1.0f / (lambda_l + lambda_v);

  float quot = 1.0f + nh2 * (alpha2 - 1.0f);
  float ggx = alpha2 / (quot * quot);

  float3 fs = (frenel * smith * ggx) / (2.0f * nv);

  constexpr float PI = radians(180.0f);

  return (nl > 0.0f) * illuminance * (fd + fs) / PI;
}
