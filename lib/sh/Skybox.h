#pragma once
#include "Std.h"

namespace ren::sh {

struct SkyboxArgs {
  DevicePtr<float> exposure;
  vec3 env_luminance;
  SH_RG_IGNORE(Handle<SamplerCube>) env_map;
  mat4 inv_proj_view;
  vec3 eye;
};

static const uint NUM_SKYBOX_VERTICES = 3;

#if __SLANG__

struct SkyboxVsOutput {
  vec4 sv_position : SV_Position;
  vec2 position : Position0;
};

#endif

} // namespace ren::sh
