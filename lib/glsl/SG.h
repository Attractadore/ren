#pragma once
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

static const uint SG_BRDF_ROUGHNESS_SIZE = 32;
static const uint SG_BRDF_NoV_SIZE = 32;
static const uint MAX_SG_BRDF_SIZE = 4;
static const uint NUM_SG_BRDF_LAYERS =
    (MAX_SG_BRDF_SIZE + 1) * MAX_SG_BRDF_SIZE / 2;
static const uint NUM_SG_BRDF_PARAMS = 4;
static const uint MAX_SG_BRDF_PARAMS = MAX_SG_BRDF_SIZE * NUM_SG_BRDF_PARAMS;

GLSL_NAMESPACE_END
