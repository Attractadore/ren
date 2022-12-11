#pragma once
#include "cpp.hlsl"

static const uint BlitTexture2DThreadsX = 16;
static const uint BlitTexture2DThreadsY = 16;

struct Texture2DBlitConfig {
  float2 src_texel_size;
};
