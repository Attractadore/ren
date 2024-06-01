#ifndef REN_GLSL_BUFFER_REFERENCE_H
#define REN_GLSL_BUFFER_REFERENCE_H

#include "Common.h"

#if GL_core_profile
#include "BufferReference.glsl"
#else
#include "BufferReference.hpp"
#endif

GLSL_NAMESPACE_BEGIN

const uint DEFAULT_BUFFER_REFERENCE_ALIGNMENT = 16;

const uint GPU_COALESCING_WIDTH = 128;

GLSL_NAMESPACE_END

#endif // REN_GLSL_BUFFER_REFERENCE_H
