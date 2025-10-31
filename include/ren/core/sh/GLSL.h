#pragma once

#if __cplusplus

#include <glm/glm.hpp>

#define SH_IN(T) const T &
#define SH_OUT(T) T &
#define SH_INOUT(T) T &

namespace ren::sh {
using namespace glm;
}

#endif

#if __SLANG__

import glsl;

#define inline
#define static_assert(expr)

#define SH_IN(T) T
#define SH_OUT(T) out T
#define SH_INOUT(T) inout T

#endif
