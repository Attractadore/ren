#ifndef REN_GLSL_MATERIAL_H
#define REN_GLSL_MATERIAL_H

#include "common.h"

REN_NAMESPACE_BEGIN

struct Material {
  vec4 base_color;
  uint base_color_texture;
  uint base_color_sampler;
  float metallic;
  float roughness;
};

REN_NAMESPACE_END

#endif // REN_GLSL_MATERIAL_H
