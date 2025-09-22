#pragma once
#include "PostProcessing.h"
#include "Std.h"

// https://bartwronski.com/2022/02/28/exposure-fusion-local-tonemapping-for-real-time-rendering/
// https://web.stanford.edu/class/cs231m/project-1/exposure-fusion.pdf

namespace ren::sh {

static const uint LTM_LIGHTNESS_GROUP_SIZE_X = 8;
static const uint LTM_LIGHTNESS_GROUP_SIZE_Y = 8;
static const uvec2 LTM_LIGHTNESS_GROUP_SIZE =
    uvec2(LTM_LIGHTNESS_GROUP_SIZE_X, LTM_LIGHTNESS_GROUP_SIZE_Y);
static const uvec2 LTM_LIGHTNESS_TILE_SIZE = LTM_LIGHTNESS_GROUP_SIZE;

struct LocalToneMappingLightnessArgs {
  Handle<Texture2D> hdr;
  Handle<RWTexture2D> lightness;
  Handle<RWTexture2D> weights;
  float middle_gray;
  ToneMapper tone_mapper;
  float shadows;
  float highlights;
  float sigma;
  uint y_offset;
  uint y_size;
};

} // namespace ren::sh
