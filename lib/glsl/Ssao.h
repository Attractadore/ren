#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SsaoArgs {
  SampledTexture2D depth;
  SampledTexture2D hi_z;
  StorageTexture2D ssao;
  uint num_samples;
  mat4 proj;
  mat4 inv_proj;
  float radius;
  float lod_bias;
}
GLSL_PC;

GLSL_NAMESPACE_END
