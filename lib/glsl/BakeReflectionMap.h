#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS BakeReflectionMapArgs {
  SampledTexture2D equirectangular_map;
  StorageTextureCube reflectance_map;
}
GLSL_PC;

GLSL_NAMESPACE_END
