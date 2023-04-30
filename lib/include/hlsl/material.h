#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

struct Material {
  float4 base_color;
  uint base_color_texture;
  uint base_color_sampler;
  float metallic;
  float roughness;
};

REN_NAMESPACE_END
