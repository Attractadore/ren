#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

#define glsl_ceil_div(nom, denom) ((nom) / (denom) + uint((nom) % (denom) != 0))

inline uint ceil_div(uint nom, uint denom) { return glsl_ceil_div(nom, denom); }

GLSL_NAMESPACE_END
