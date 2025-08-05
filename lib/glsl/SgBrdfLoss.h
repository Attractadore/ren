#pragma once
#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

static const uint SG_BRDF_LOSS_THREADS_X = 256;

enum class SgBrdfLossPdf {
  GGX,
  UniformSphere,
  Fresnel,
  Count,
};

static const uint NUM_SG_BRDF_LOSS_F0 = 3;
static const float SG_BRDF_LOSS_F0[NUM_SG_BRDF_LOSS_F0] = {
    0.04f,
    0.5f,
    1.0f,
};
static const float SG_BRDF_LOSS_F0_WEIGHTS[NUM_SG_BRDF_LOSS_F0] = {
    2.0f,
    1.0f,
    1.0f,
};

struct SgBrdfLossArgs {
  GLSL_PTR(float) f_norm_lut;
  GLSL_PTR(float) params;
  GLSL_PTR(FloatBox) loss;
  GLSL_PTR(FloatBox) grad;
  float NoV;
  float roughness;
  uint n;
  uint g;
};

GLSL_NAMESPACE_END
