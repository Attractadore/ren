#pragma once
#include "Std.h"

namespace ren::sh {

struct ImGuiVertex {
  vec2 position;
  vec2 uv;
  uint32_t color;
};

struct ImGuiArgs {
  DevicePtr<ImGuiVertex> vertices;
  vec2 scale;
  vec2 translate;
  Handle<Sampler2D> tex;
};

#if __SLANG__

struct ImGuiVsOutput {
  vec4 sv_position : SV_Position;
  vec4 color : Color0;
  vec2 uv : TexCoord0;
};

#endif

} // namespace ren::sh
