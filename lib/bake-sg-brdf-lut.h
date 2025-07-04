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

static const uint MAX_NUM_SGS = 4;
static const uint NUM_PARAMS = 4;
static const double MIN_F0 = 0.02;

struct DASG I_DIFFERENTIABLE {
  dvec3 z;
  dvec3 x;
  dvec3 y;
  double a;
  double lx;
  double ly;
};

DIFFERENTIABLE
inline double eval_asg(DASG asg, dvec3 V) {
  double VoX = dot(asg.x, V);
  double VoY = dot(asg.y, V);
  return asg.a * max(dot(asg.z, V), 0.0) *
         exp(-asg.lx * VoX * VoX - asg.ly * VoY * VoY);
}

static const uint F_NORM_LUT_SIZE = 256;
GLOBAL EXTERN_C double F_NORM_LUT[F_NORM_LUT_SIZE];

DIFFERENTIABLE
inline double F_norm(double f0, double NoV) {
  int i = round(f0 * double(F_NORM_LUT_SIZE - 1));
  return F_NORM_LUT[i] * F_schlick(f0, NoV);
};

DIFFERENTIABLE
inline DASG make_asg(double phi, double a, double lx, double ly, double f0,
                     double roughness, dvec3 V) {
  dvec3 Z = {cos(phi), 0, sin(phi)};
  dvec3 Y = dvec3(0, 1, 0);
  dvec3 X = {-sin(phi), 0, cos(phi)};
  dvec3 H = normalize(Z + V);
  double VoH = dot(V, H);
  double NoV = V.z;
  double NoL = Z.z;
  double NoH = H.z;
  DASG asg = {};
  asg.z = Z;
  asg.x = X;
  asg.y = Y;
  asg.a = a * F_norm(f0, VoH) * D_ggx(roughness, NoH);
  double alpha2 = roughness * roughness;
  alpha2 = alpha2 * alpha2;
  double l = 2 / alpha2;
  asg.lx = lx * (l / 8);
  asg.ly = ly * (l / (8 * NoV * NoV));
  return asg;
};

PUBLIC struct SgBrdfLossArgs {
  double NoV;
  double roughness;
  uint n;
  uint g;
  const double *params;
  double *grad;
};

EXTERN_C double ren_sg_brdf_loss(SgBrdfLossArgs args);

GLSL_NAMESPACE_END
