#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SsaoArgs {
  SampledTexture2D depth;
  SampledTexture2D hi_z;
  StorageTexture2D ssao;
  StorageTexture2D ssao_depth;
  uint num_samples;
  float p00;
  float p11;
  float znear;
  float rcp_p00;
  float rcp_p11;
  float radius;
  float lod_bias;
}
GLSL_PC;

GLSL_NAMESPACE_END
