#pragma once
#include "DevicePtr.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

static const uint F_NORM_LUT_SIZE = 256;
static const uint MAX_NUM_SGS = 4;
static const uint NUM_PARAMS = 4;
static const uint MAX_NUM_PARAMS = MAX_NUM_SGS * NUM_PARAMS;
static const double MIN_F0 = 0.02;

#if SLANG
struct DoubleBox {
  double value;
};
#elif __cplusplus
using DoubleBox = double;
#endif

struct SgBrdfLossArgs {
  GLSL_PTR(double) f_norm_lut;
  GLSL_PTR(double) params;
  GLSL_PTR(DoubleBox) loss;
  GLSL_PTR(DoubleBox) loss0;
  GLSL_PTR(DoubleBox) grad;
  double NoV;
  double roughness;
  uint n;
  uint g;
};

GLSL_NAMESPACE_END
