#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SgEnvLightingPreviewArgs {
  uint num_sgs;
  GLSL_PTR(float) raw_params;
  StorageTextureCube preview_map;
}
GLSL_PC;

GLSL_NAMESPACE_END
