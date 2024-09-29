#ifndef REN_GLSL_IMGUI_PASS_H
#define REN_GLSL_IMGUI_PASS_H

#include "Common.h"
#include "DevicePtr.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

struct ImGuiVertex {
  vec2 position;
  vec2 uv;
  uint32_t color;
};

GLSL_DEFINE_PTR_TYPE(ImGuiVertex, 4);

struct ImGuiPassArgs {
  GLSL_PTR(ImGuiVertex) vertices;
  vec2 scale;
  vec2 translate;
  SampledTexture2D tex;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_IMGUI_PASS_H
