#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SsaoArgs {
  SampledTexture2D depth;
  StorageTexture2D ssao;
  mat4 proj;
  mat4 inv_proj;
  float radius;
  uint num_samples;
}
GLSL_PC;

GLSL_NAMESPACE_END
