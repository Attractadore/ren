#pragma once
#include <cstdint>
#include <glm/glm.hpp>

#define REN_BUFFER_REFERENCE(alignment) struct
#define REN_REFERENCE(type) uint64_t

#define REN_NAMESPACE_BEGIN namespace ren::glsl {
#define REN_NAMESPACE_END }

REN_NAMESPACE_BEGIN
using namespace glm;
REN_NAMESPACE_END
