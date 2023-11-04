#ifndef REN_GLSL_COMMON_H
#define REN_GLSL_COMMON_H

#if GL_core_profile
#include "common.glsl"
#include "stddef.glsl"
#else
#include "common.hpp"
#include "stddef.hpp"
#endif

#endif // REN_GLSL_COMMON_H
