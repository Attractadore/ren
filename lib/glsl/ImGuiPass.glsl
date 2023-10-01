#ifndef REN_GLSL_IMGUI_GLSL
#define REN_GLSL_IMGUI_GLSL

#include "ImGuiPass.h"

#define VS_OUT { \
  vec4 color; \
  vec2 tex_coord; \
}

#define FS_IN VS_OUT

#endif // REN_GLSL_IMGUI_GLSL
