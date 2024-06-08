#ifndef REN_GLSL_DEVICE_PTR_H
#define REN_GLSL_DEVICE_PTR_H

#include "Common.h"

#if GL_core_profile
#include "DevicePtr.glsl"
#else
#include "DevicePtr.hpp"
#endif

GLSL_NAMESPACE_BEGIN

const uint DEFAULT_DEVICE_PTR_ALIGNMENT = 16;

const uint DEVICE_CACHE_LINE_SIZE = 128;

GLSL_DEFINE_PTR_TYPE(uint, 4);

GLSL_DEFINE_PTR_TYPE(mat4x3, 4);

GLSL_DEFINE_PTR_TYPE(mat3, 4);

GLSL_NAMESPACE_END

#endif // REN_GLSL_DEVICE_PTR_H
