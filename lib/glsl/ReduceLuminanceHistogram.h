#ifndef REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_H
#define REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_H

#include "DevicePtr.h"
#include "LuminanceHistogram.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS ReduceLuminanceHistogramArgs {
  GLSL_READONLY GLSL_PTR(LuminanceHistogram) histogram;
  StorageTexture2D exposure;
  float exposure_compensation;
}
GLSL_PC;

GLSL_NAMESPACE_END

#endif // REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_H
