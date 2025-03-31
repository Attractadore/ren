#pragma once
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SkyboxArgs {
  Texture2D exposure;
  vec3 env_luminance;
  SampledTextureCube raw_env_map;
  mat4 inv_proj_view;
  vec3 eye;
}
GLSL_PC;

const uint NUM_SKYBOX_VERTICES = 3;

GLSL_NAMESPACE_END
