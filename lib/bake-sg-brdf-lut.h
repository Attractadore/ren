#pragma once
#include "glsl/BRDF.h"
#include "glsl/Std.h"

GLSL_NAMESPACE_BEGIN

#ifdef __cplusplus
#define EXTERN_C extern "C"
#define PUBLIC
#define GLOBAL
#else
#define EXTERN_C export __extern_cpp
#define PUBLIC public
#define GLOBAL __global
#endif

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

static const uint F_NORM_LUT_SIZE = 128;
GLOBAL EXTERN_C float F_NORM_LUT[F_NORM_LUT_SIZE];

DIFFERENTIABLE
inline float F_norm(float f0, float NoV) {
  int i = round(f0 * float(F_NORM_LUT_SIZE - 1));
  return F_NORM_LUT[i] * F_schlick(f0, NoV);
};

DIFFERENTIABLE
inline ASG make_asg(float phi, float a, float lx, float ly, float f0, vec3 V,
                    vec3 B) {
  vec3 Z = {cos(phi), 0, sin(phi)};
  vec3 Y = B;
  vec3 X = {-sin(phi), 0, cos(phi)};
  vec3 H = normalize(Z + V);
  ASG asg = {};
  asg.z = Z;
  asg.x = X;
  asg.y = Y;
  asg.a = a * F_norm(f0, dot(H, V));
  asg.lx = lx;
  asg.ly = ly;
  return asg;
};

static const uint MAX_NUM_SGS = 4;
static const uint NUM_PARAMS = 4;

PUBLIC struct SgBrdfLossArgs {
  vec3 V;
  vec3 B;
  uint n;
  const float *f0;
  const vec3 *L;
  const float *y;
  uint g;
  const float *params;
  float *grad;
};

EXTERN_C float ren_sg_brdf_loss(SgBrdfLossArgs args);

GLSL_NAMESPACE_END
