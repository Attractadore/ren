#pragma once
#include "Std.h"

namespace ren::sh {

static const uint SSAO_HILBERT_CURVE_LEVEL = 6;
static const uint SSAO_HILBERT_CURVE_SIZE = 1 << SSAO_HILBERT_CURVE_LEVEL;

struct SsaoArgs {
  SH_RG_IGNORE(DevicePtr<float>) noise_lut;
  Handle<Sampler2D> depth;
  Handle<Sampler2D> hi_z;
  Handle<RWTexture2D> ssao;
  Handle<RWTexture2D> ssao_depth;
  uint num_samples;
  float p00;
  float p11;
  float znear;
  float rcp_p00;
  float rcp_p11;
  float radius;
  float lod_bias;
};

} // namespace ren::sh
