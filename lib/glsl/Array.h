#ifndef REN_GLSL_ARRAY_H
#define REN_GLSL_ARRAY_H

#if GL_core_profile

#define GLSL_ARRAY(T, name, SIZE) T name[SIZE]

#else

#include <array>

#define GLSL_ARRAY(T, name, SIZE) std::array<T, SIZE> name

#endif

#endif // REN_GLSL_ARRAY_H
