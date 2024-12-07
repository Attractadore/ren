#pragma once
#include <boost/predef/compiler.h>

#define ren_cat_impl(a, b) a##b
#define ren_cat(a, b) ren_cat_impl(a, b)

#if BOOST_COMP_GNUC || BOOST_COMP_CLANG
#define ren_trap __builtin_trap
#else
#define ren_trap abort
#endif
