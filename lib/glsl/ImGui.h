#pragma once
#include "DevicePtr.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

struct ImGuiVertex {
  vec2 position;
  vec2 uv;
  uint32_t color;
};

GLSL_DEFINE_PTR_TYPE(ImGuiVertex, 4);

GLSL_PUSH_CONSTANTS ImGuiArgs {
  GLSL_READONLY GLSL_PTR(ImGuiVertex) vertices;
  vec2 scale;
  vec2 translate;
  SampledTexture2D tex;
}
GLSL_PC;

#if GL_core_profile

const uint A_COLOR = 0;
const uint A_UV = 1;

#endif

GLSL_NAMESPACE_END
