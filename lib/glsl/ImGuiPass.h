#ifndef REN_GLSL_IMGUI_H
#define REN_GLSL_IMGUI_H

#include "BufferReference.h"
#include "Common.h"

GLSL_NAMESPACE_BEGIN

struct ImGuiVertex {
  vec2 position;
  vec2 uv;
  uint32_t color;
};

GLSL_REF_TYPE(4) ImGuiVertexRef { ImGuiVertex vertex; };

struct ImGuiPassArgs {
  GLSL_REF(ImGuiVertexRef) vertices;
  vec2 scale;
  vec2 translate;
  uint tex;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_IMGUI_H
