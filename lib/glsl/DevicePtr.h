#pragma once
#include "Std.h"

#if GL_core_profile
#include "DevicePtr.glsl"
#else
#include "DevicePtr.hpp"
#endif

GLSL_NAMESPACE_BEGIN

GLSL_DEFINE_PTR_TYPE(int, 4);
GLSL_DEFINE_PTR_TYPE(int8_t, 1);
GLSL_DEFINE_PTR_TYPE(int16_t, 2);
GLSL_DEFINE_PTR_TYPE(int32_t, 4);
GLSL_DEFINE_PTR_TYPE(int64_t, 8);

GLSL_DEFINE_PTR_TYPE(uint, 4);
GLSL_DEFINE_PTR_TYPE(uint8_t, 1);
GLSL_DEFINE_PTR_TYPE(uint16_t, 2);
GLSL_DEFINE_PTR_TYPE(uint32_t, 4);
GLSL_DEFINE_PTR_TYPE(uint64_t, 8);

GLSL_DEFINE_PTR_TYPE(float, 4);
GLSL_DEFINE_PTR_TYPE(vec2, 4);
GLSL_DEFINE_PTR_TYPE(vec3, 4);
GLSL_DEFINE_PTR_TYPE(vec4, 4);

GLSL_DEFINE_PTR_TYPE(mat3, 4);
GLSL_DEFINE_PTR_TYPE(mat4x3, 4);
GLSL_DEFINE_PTR_TYPE(mat4, 4);

#if SLANG
#define GLSL_PTR(Type) Type *
#else
#define GLSL_PTR(Type) GLSL_RESTRICT GLSL_UNQUALIFIED_PTR(Type)
#endif

// Workaround for compiler not generating correct code for stores through
// pointers.
#if SLANG
struct FloatBox {
  float value;
};
#elif __cplusplus
using FloatBox = float;
#else
#define FloatBox float
#endif

GLSL_NAMESPACE_END
