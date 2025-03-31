#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS BakeSpecularMapArgs {
  SampledTexture2D equirectangular_map;
  StorageTextureCube specular_map;
  float roughness;
}
GLSL_PC;

GLSL_NAMESPACE_END
