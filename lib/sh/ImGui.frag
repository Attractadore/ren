#include "ImGui.h"

namespace ren::sh {

[[vk::push_constant]] ImGuiArgs pc;

vec4 main(ImGuiVsOutput in) : SV_Target0 {
  return in.color * Get(pc.tex).Sample(in.uv);
}

}
