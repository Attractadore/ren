#pragma once
#include "PostProcessing.h"
#include "Std.h"

// https://bartwronski.com/2022/02/28/exposure-fusion-local-tonemapping-for-real-time-rendering/
// https://web.stanford.edu/class/cs231m/project-1/exposure-fusion.pdf

namespace ren::sh {

static const uint LTM_GROUP_SIZE_X = 16;
static const uint LTM_GROUP_SIZE_Y = 16;
static const uvec2 LTM_GROUP_SIZE = uvec2(LTM_GROUP_SIZE_X, LTM_GROUP_SIZE_Y);
static const uint LTM_UNROLL_X = 2;
static const uint LTM_UNROLL_Y = 2;
static const uvec2 LTM_UNROLL = uvec2(LTM_UNROLL_X, LTM_UNROLL_Y);
static const uint LTM_PYRAMID_SIZE = 6;
static_assert(LTM_GROUP_SIZE_X * LTM_UNROLL_X == 1 << (LTM_PYRAMID_SIZE - 1));
static_assert(LTM_GROUP_SIZE_Y * LTM_UNROLL_Y == 1 << (LTM_PYRAMID_SIZE - 1));

struct LocalToneMappingArgs {
  DevicePtr<float> exposure;
  SH_RG_IGNORE(DevicePtr<vec3>) noise_lut;
  Handle<Texture2D> hdr;
  SH_ARRAY(Handle<RWTexture2D>, lightness, LTM_PYRAMID_SIZE);
  SH_ARRAY(Handle<RWTexture2D>, weights, LTM_PYRAMID_SIZE);
  float middle_gray;
  ToneMapper tone_mapper;
  float shadows;
  float highlights;
  float sigma;
};

} // namespace ren::sh
