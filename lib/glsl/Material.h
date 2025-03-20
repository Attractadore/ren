#ifndef REN_GLSL_MATERIAL_H
#define REN_GLSL_MATERIAL_H

#include "DevicePtr.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

struct Material {
  vec4 base_color;
  SampledTexture2D base_color_texture;
  float occlusion_strength;
  float roughness;
  float metallic;
  SampledTexture2D orm_texture;
  float normal_scale;
  SampledTexture2D normal_texture;
};

GLSL_DEFINE_PTR_TYPE(Material, 4);

GLSL_NAMESPACE_END

#endif // REN_GLSL_MATERIAL_H
