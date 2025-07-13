#pragma once
#include "Math.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

struct ASG I_DIFFERENTIABLE {
  vec3 z;
  vec3 x;
  vec3 y;
  float a;
  float lx;
  float ly;
};

DIFFERENTIABLE
inline float eval_asg(ASG asg, vec3 V) {
  float VoX = dot(asg.x, V);
  float VoY = dot(asg.y, V);
  return asg.a * max(dot(asg.z, V), 0.0f) *
         exp(-asg.lx * VoX * VoX - asg.ly * VoY * VoY);
}

#if 0
static const uint NUM_SG_BRDF_SAMPLE_POINTS = 16 * 1024;
static const uint SG_BRDF_ROUGHNESS_SIZE = 32;
static const uint SG_BRDF_NoV_SIZE = 32;
#else
static const uint NUM_SG_BRDF_SAMPLE_POINTS = 1024;
static const uint SG_BRDF_ROUGHNESS_SIZE = 16;
static const uint SG_BRDF_NvV_SIZE = 32;
#endif

static const uint MAX_SG_BRDF_SIZE = 4;
static const uint NUM_SG_BRDF_LAYERS =
    (MAX_SG_BRDF_SIZE + 1) * MAX_SG_BRDF_SIZE / 2;
static const uint NUM_SG_BRDF_PARAMS = 4;
static const uint MAX_SG_BRDF_PARAMS = MAX_SG_BRDF_SIZE * NUM_SG_BRDF_PARAMS;
static const float MIN_SG_BRDF_ROUGHNESS = 0.1f;

inline vec2 sg_brdf_r_and_NvV_to_uv(float roughness, float phi) {
  float uv_x =
      (roughness - MIN_SG_BRDF_ROUGHNESS) / (1.0f - MIN_SG_BRDF_ROUGHNESS);
  float phi_norm = phi / (0.5f * PI);
  float uv_y = mix(0.5f / SG_BRDF_NvV_SIZE, 1.0f, phi_norm);
  return vec2(uv_x, uv_y);
}

inline float sg_brdf_uv_to_r(float uv_x) {
  return mix(MIN_SG_BRDF_ROUGHNESS, 1.0f, uv_x);
}

inline float sg_brdf_uv_to_NvV(float uv_y) {
  float phi_norm = (uv_y * SG_BRDF_NvV_SIZE - 0.5f) / (SG_BRDF_NvV_SIZE - 0.5f);
  return 0.5f * PI * clamp(phi_norm, 0.0f, 1.0f);
}

inline vec2 sg_brdf_uv_to_r_and_NvV(vec2 uv) {
  return vec2(sg_brdf_uv_to_r(uv.x), sg_brdf_uv_to_NvV(uv.y));
}

GLSL_NAMESPACE_END
