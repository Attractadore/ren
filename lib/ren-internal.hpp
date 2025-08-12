#pragma once
#include "ren/ren.hpp"

#if REN_HOT_RELOAD

namespace ren::hot_reload {

struct VtblInternal {
#define ren_vtbl_f(name) decltype(ren::hot_reload::name) name
  ren_vtbl_f(pre_reload_hook);
  ren_vtbl_f(post_reload_hook);
#undef ren_vtbl_f
};

} // namespace ren::hot_reload

extern "C" ren::hot_reload::Vtbl ren_vtbl;
extern "C" ren::hot_reload::VtblInternal ren_vtbl_internal;

#endif
