#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

inline mat3 adjugate(mat3 m) {
  return mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
}

inline mat3 normal(mat3 m) { return adjugate(m); }

GLSL_NAMESPACE_END
