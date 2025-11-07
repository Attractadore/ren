#pragma once
#if _WIN32
#include "NotNull.hpp"
#include "StdDef.hpp"

#include <intrin.h>

namespace ren {

struct Win32FiberContext {
  void (*rip)();
  void *rsp;
  u64 rdi;
  u64 rsi;
  u64 rbx;
  u64 rbp;
  u64 r12;
  u64 r13;
  u64 r14;
  u64 r15;
  __m128 xmm6;
  __m128 xmm7;
  __m128 xmm8;
  __m128 xmm9;
  __m128 xmm10;
  __m128 xmm11;
  __m128 xmm12;
  __m128 xmm13;
  __m128 xmm14;
  __m128 xmm15;
};
using FiberContext = Win32FiberContext;

extern "C" void fiber_save_context_x64(Win32FiberContext *context);

extern "C" void fiber_load_context_x64(const Win32FiberContext *context);

extern "C" void
fiber_switch_context_x64(Win32FiberContext *this_context,
                         const Win32FiberContext *other_context);

ALWAYS_INLINE void fiber_save_context(NotNull<Win32FiberContext *> context) {
  fiber_save_context_x64(context);
}

ALWAYS_INLINE void fiber_load_context(Win32FiberContext context) {
  fiber_load_context_x64(&context);
}

ALWAYS_INLINE void
fiber_switch_context(NotNull<Win32FiberContext *> this_context,
                     Win32FiberContext other_context) {
  fiber_switch_context_x64(this_context, &other_context);
}

} // namespace ren

#endif
