#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SsaoBlurArgs {
  Texture2D depth;
  Texture2D ssao;
  Texture2D ssao_depth;
  StorageTexture2D ssao_blurred;
  float znear;
  float radius;
}
GLSL_PC;

const uint SSAO_BLUR_THREAD_ITEMS_X = 2;
const uint SSAO_BLUR_THREAD_ITEMS_Y = 2;

GLSL_NAMESPACE_END
