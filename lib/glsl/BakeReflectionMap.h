#ifndef REN_GLSL_BAKE_REFLECTION_MAP_H
#define REN_GLSL_BAKE_REFLECTION_MAP_H

#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS BakeReflectionMapArgs {
  SampledTexture2D equirectangular_map;
  StorageTextureCube reflectance_map;
}
GLSL_PC;

GLSL_NAMESPACE_END

#endif // REN_GLSL_BAKE_REFLECTION_MAP_H
