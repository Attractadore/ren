#pragma once
#include "DevicePtr.h"
#include "LuminanceHistogram.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS ReduceLuminanceHistogramArgs {
  GLSL_READONLY GLSL_PTR(LuminanceHistogram) histogram;
  GLSL_PTR(float) exposure;
  float exposure_compensation;
}
GLSL_PC;

GLSL_NAMESPACE_END
