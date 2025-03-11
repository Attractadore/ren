#ifndef REN_GLSL_SKYBOX_H
#define REN_GLSL_SKYBOX_H

#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS SkyboxArgs {
  Texture2D exposure;
  vec3 env_luminance;
}
GLSL_PC;

const uint NUM_SKYBOX_VERTICES = 3;

GLSL_NAMESPACE_END

#endif // REN_GLSL_SKYBOX_H
