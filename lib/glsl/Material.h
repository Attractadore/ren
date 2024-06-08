#ifndef REN_GLSL_MATERIAL_H
#define REN_GLSL_MATERIAL_H

#include "Common.h"
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

struct Material {
  vec4 base_color;
  uint base_color_texture;
  float metallic;
  float roughness;
  uint metallic_roughness_texture;
  uint normal_texture;
  float normal_scale;
};

GLSL_DEFINE_PTR_TYPE(Material, 4);

GLSL_NAMESPACE_END

#endif // REN_GLSL_MATERIAL_H
