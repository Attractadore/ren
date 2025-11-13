#pragma once

#if __clang__
#pragma clang system_header
#endif

#if __GNUC__
#pragma GCC system_header
#endif

#if _MSC_VER
#pragma system_header
#endif

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#define constexpr
#include <tiny_imageformat/tinyimageformat.h>
#undef constexpr
