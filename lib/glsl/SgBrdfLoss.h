#pragma once
#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

static const uint F_NORM_LUT_SIZE = 256;
static const uint MAX_NUM_SGS = 4;
static const uint NUM_PARAMS = 4;
static const uint MAX_NUM_PARAMS = MAX_NUM_SGS * NUM_PARAMS;
static const float MIN_F0 = 0.02;

// Workaround for compiler not generating correct code for stores through
// pointers.
#if SLANG
struct FloatBox {
  float value;
};
#elif __cplusplus
using FloatBox = float;
#endif

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
