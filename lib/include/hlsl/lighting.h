#pragma once
#include "cpp.h"
#include "material.h"

REN_NAMESPACE_BEGIN

struct DirLight {
  float3 color;
  float illuminance;
  float3 origin;
};

REN_NAMESPACE_END
