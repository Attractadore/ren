#ifndef REN_GLSL_LIGHTING_H
#define REN_GLSL_LIGHTING_H

#include "Common.h"
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

struct DirLight {
  vec3 color;
  float illuminance;
  vec3 origin;
};

GLSL_DEFINE_PTR_TYPE(DirLight, 4);

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

  vec3 fd = mix(color, vec3(0.0f), metallic);

  float ior = 1.5f;
  vec3 f0 = vec3((ior - 1.0f) / (ior + 1.0f));
  f0 = f0 * f0;
  f0 = mix(f0, color, metallic);

  vec3 frenel = f0 + (1.0f - f0) * pow(1.0f - lh, 5.0f);

  float lambda_l = sqrt(1.0f + alpha2 * (1.0f - nl2) / nl2);
  float lambda_v = sqrt(1.0f + alpha2 * (1.0f - nv2) / nv2);

  float smith = 1.0f / (lambda_l + lambda_v);

  float quot = 1.0f + nh2 * (alpha2 - 1.0f);
  float ggx = alpha2 / (quot * quot);

  vec3 fs = (frenel * smith * ggx) / (2.0f * nv);

  const float PI = radians(180.0f);

  vec3 direct = float(nl > 0.0f) * illuminance * (fd + fs);
  vec3 indirect = 0.1f * illuminance * fd;

  return (direct + indirect) / PI;
}

GLSL_NAMESPACE_END

#endif // REN_GLSL_LIGHTING_H
