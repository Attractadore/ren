#pragma once
#include "../BufferReference.hpp"

#include <glm/glm.hpp>

#include <cstdint>

#define GLSL_NAMESPACE_BEGIN namespace ren::glsl {
#define GLSL_NAMESPACE_END }

GLSL_NAMESPACE_BEGIN
using namespace glm;
GLSL_NAMESPACE_END

#define GLSL_BUFFER(alignment) struct alignas(alignment)

#define GLSL_BUFFER_REFERENCE(type) ren::BufferReference<type>

#define GLSL_RESTRICT
#define GLSL_READONLY
#define GLSL_WRITEONLY
