#pragma once
#include "DevicePtr.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

const uint SSAO_HILBERT_CURVE_LEVEL = 6;
const uint SSAO_HILBERT_CURVE_SIZE = 1 << SSAO_HILBERT_CURVE_LEVEL;

GLSL_PUSH_CONSTANTS SsaoArgs {
  GLSL_READONLY GLSL_PTR(float) raw_noise_lut;
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
