#pragma once
#include "../BufferReference.hpp"

#include <glm/glm.hpp>

#include <cstdint>

#define REN_NAMESPACE_BEGIN namespace ren::glsl {
#define REN_NAMESPACE_END }

REN_NAMESPACE_BEGIN
using namespace glm;
REN_NAMESPACE_END

#define REN_BUFFER_REFERENCE(alignment) struct alignas(alignment)
#define REN_REFERENCE(type) ren::BufferReference<type>
