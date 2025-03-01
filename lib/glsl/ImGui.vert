#include "ImGui.h"

layout(location = A_COLOR) out vec4 a_color;
layout(location = A_UV) out vec2 a_uv;

vec4 decode_color(uint32_t color) {
  uint r = (color & 0x000000FF) >>  0;
  uint g = (color & 0x0000FF00) >>  8;
  uint b = (color & 0x00FF0000) >> 16;
  uint a = (color & 0xFF000000) >> 24;
  return vec4(float(r), float(g), float(b), float(a)) / 255.0f;
}

void main()
{
    ImGuiVertex vertex = DEREF(pc.vertices[gl_VertexIndex]);
    a_color = decode_color(vertex.color);
    a_uv = vertex.uv;
    gl_Position = vec4(vertex.position * pc.scale + pc.translate, 0.0f, 1.0f);
}
