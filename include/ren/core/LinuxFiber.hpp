#pragma once
#if __linux__
#include "NotNull.hpp"
#include "StdDef.hpp"

namespace ren {

// System V ABI.
struct LinuxFiberContext {
  void (*rip)();
  void *rsp;
  u64 rbx;
  u64 rbp;
  u64 r12;
  u64 r13;
  u64 r14;
  u64 r15;
};
using FiberContext = LinuxFiberContext;

extern "C" void fiber_save_context_system_v(LinuxFiberContext *context);

extern "C" void fiber_load_context_system_v(const LinuxFiberContext *context);

extern "C" void
fiber_switch_context_system_v(LinuxFiberContext *this_context,
                              const LinuxFiberContext *other_context);

ALWAYS_INLINE void fiber_save_context(NotNull<LinuxFiberContext *> context) {
  fiber_save_context_system_v(context);
}

ALWAYS_INLINE void fiber_load_context(LinuxFiberContext context) {
  fiber_load_context_system_v(&context);
}

ALWAYS_INLINE void
fiber_switch_context(NotNull<LinuxFiberContext *> this_context,
                     LinuxFiberContext other_context) {
  fiber_switch_context_system_v(this_context, &other_context);
}

} // namespace ren

#endif
