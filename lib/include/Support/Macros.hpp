#pragma once

#define ren_cat_impl(a, b) a##b
#define ren_cat(a, b) ren_cat_impl(a, b)
#define ren_force_semicolon                                                    \
  do {                                                                         \
  } while (0)
