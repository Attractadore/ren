#pragma once
#include <cstdlib>

#define ren_cat_impl(a, b) a##b
#define ren_cat(a, b) ren_cat_impl(a, b)

#if __GNUC__
#define ren_trap __builtin_trap
#elif _MSC_VER
#define ren_trap __debugbreak
#else
#define ren_trap std::abort
#endif
