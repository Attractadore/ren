#ifndef BUILD_DHR_LUT_H
#define BUILD_DHR_LUT_H

#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS ComputeDHRLutArgs { StorageTexture2D lut; }
GLSL_PC;

GLSL_NAMESPACE_END

#endif // BUILD_DHR_LUT_H
