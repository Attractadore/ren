#pragma once
#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

static const uint SG_ENV_LIGHTING_LOSS_THREADS = 256;

struct SgEnvLightingLossArgs {
  uint num_sgs;
  GLSL_PTR(float) params;
  GLSL_PTR(FloatBox) grad;
  GLSL_PTR(FloatBox) loss;
  GLSL_PTR(float) luminance;
};

GLSL_NAMESPACE_END
