#pragma once
#include "cpp.h"
#include "material.h"

REN_NAMESPACE_BEGIN

struct DirLight {
  float3 color;
  float illuminance;
  float3 origin;
};

constexpr uint MAX_DIRECTIONAL_LIGHTS = 8;

struct Lights {
  uint num_dir_lights;
  DirLight dir_lights[MAX_DIRECTIONAL_LIGHTS];
};

REN_NAMESPACE_END
