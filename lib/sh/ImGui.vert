#include "ImGui.h"

namespace ren::sh {

[[vk::push_constant]] ImGuiArgs pc;

vec4 decode_color(uint32_t color) {
  uint r = (color & 0x000000FF) >>  0;
  uint g = (color & 0x0000FF00) >>  8;
  uint b = (color & 0x00FF0000) >> 16;
  uint a = (color & 0xFF000000) >> 24;
  return vec4(float(r), float(g), float(b), float(a)) / 255.0f;
}

ImGuiVsOutput main() {
  ImGuiVertex vertex = pc.vertices[gl_VertexIndex];
  ImGuiVsOutput out;
  out.sv_position = vec4(vertex.position * pc.scale + pc.translate, 0.0f, 1.0f);
  out.color = decode_color(vertex.color); 
  out.uv = vertex.uv;
  return out;
}

}
