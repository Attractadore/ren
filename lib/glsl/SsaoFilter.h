#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SsaoFilterArgs {
  Texture2D depth;
  Texture2D ssao;
  Texture2D ssao_depth;
  StorageTexture2D ssao_llm;
  float znear;
}
GLSL_PC;

constexpr uint SSAO_FILTER_RADIUS = 4;

constexpr uvec2 SSAO_FILTER_GROUP_SIZE = uvec2(8, 8);
constexpr uvec2 SSAO_FILTER_UNROLL = uvec2(1, 16);

static_assert(SSAO_FILTER_RADIUS <= SSAO_FILTER_GROUP_SIZE.y);

GLSL_NAMESPACE_END
