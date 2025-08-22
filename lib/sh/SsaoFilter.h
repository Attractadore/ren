#pragma once
#include "Std.h"

namespace ren::sh {

struct SsaoFilterArgs {
  Handle<Texture2D> depth;
  Handle<Texture2D> ssao;
  Handle<Texture2D> ssao_depth;
  Handle<RWTexture2D> ssao_llm;
  float znear;
};

static const uint SSAO_FILTER_RADIUS = 4;

static const uint SSAO_FILTER_GROUP_SIZE_X = 8;
static const uint SSAO_FILTER_GROUP_SIZE_Y = 8;
static const uvec2 SSAO_FILTER_GROUP_SIZE =
    uvec2(SSAO_FILTER_GROUP_SIZE_X, SSAO_FILTER_GROUP_SIZE_Y);

static const uint SSAO_FILTER_UNROLL_X = 1;
static const uint SSAO_FILTER_UNROLL_Y = 16;
static const uvec2 SSAO_FILTER_UNROLL =
    uvec2(SSAO_FILTER_UNROLL_X, SSAO_FILTER_UNROLL_Y);

static_assert(SSAO_FILTER_RADIUS <= SSAO_FILTER_GROUP_SIZE_Y);

} // namespace ren::sh
