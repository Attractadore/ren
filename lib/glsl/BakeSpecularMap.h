#ifndef REN_GLSL_BAKE_SPECULAR_MAP_H
#define REN_GLSL_BAKE_SPECULAR_MAP_H

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

#endif // REN_GLSL_BAKE_SPECULAR_MAP_H
