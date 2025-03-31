#pragma once
#if GL_core_profile

#define GLSL_ARRAY(T, name, SIZE) T name[SIZE]

#else

#include <array>

#define GLSL_ARRAY(T, name, SIZE) std::array<T, SIZE> name

#endif
