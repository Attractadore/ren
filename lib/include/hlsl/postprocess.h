#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

inline float get_luminance(float3 color) {
  return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

REN_NAMESPACE_END
