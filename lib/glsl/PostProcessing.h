#pragma once
#include "DevicePtr.h"
#include "LuminanceHistogram.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS PostProcessingArgs {
  GLSL_PTR(LuminanceHistogram) histogram;
  Texture2D previous_exposure;
  Texture2D hdr;
  StorageTexture2D sdr;
}
GLSL_PC;

GLSL_NAMESPACE_END
