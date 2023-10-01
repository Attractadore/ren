#ifndef REN_GLSL_IMGUI_H
#define REN_GLSL_IMGUI_H

#include "common.h"

GLSL_NAMESPACE_BEGIN

struct ImGuiVertex {
  vec2 position;
  vec2 tex_coord;
  uint32_t color;
};

GLSL_BUFFER(4) ImGuiVertices { ImGuiVertex vertex; };

#define GLSL_IMGUI_CONSTANTS                                                   \
  {                                                                            \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(ImGuiVertices) vertices; \
    vec2 scale;                                                                \
    vec2 translate;                                                            \
    uint tex;                                                                  \
  }

GLSL_NAMESPACE_END

#endif // REN_GLSL_IMGUI_H
