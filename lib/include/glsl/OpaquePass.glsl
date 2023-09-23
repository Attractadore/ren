#ifndef REN_GLSL_OPAQUE_PASS_GLSL
#define REN_GLSL_OPAQUE_PASS_GLSL

#include "OpaquePass.h"

#define VS_OUT                                                                 \
  {                                                                            \
    vec3 position;                                                             \
    vec4 color;                                                                \
    vec3 normal;                                                               \
    vec2 uv;                                                                   \
    flat uint material;                                                        \
  }

#define FS_IN VS_OUT

#endif // REN_GLSL_OPAQUE_PASS_GLSL
