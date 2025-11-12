#pragma once
#include "NotNull.hpp"
#include "StdDef.hpp"

#include <atomic>
#include <cstdio>
#include <tracy/Tracy.hpp>

namespace ren {

constexpr usize FIBER_STACK_ALIGNMENT = 16;

extern "C" void __sanitizer_start_switch_fiber(void **fake_stack_save,
                                               const void *bottom, size_t size);

extern "C" void __sanitizer_finish_switch_fiber(void *fake_stack_save,
                                                const void **bottom_old,
                                                size_t *size_old);

#if !REN_ASAN

extern "C" ALWAYS_INLINE void
__sanitizer_start_switch_fiber(void **fake_stack_save, const void *bottom,
                               size_t size) {}

extern "C" ALWAYS_INLINE void
__sanitizer_finish_switch_fiber(void *fake_stack_save, const void **bottom_old,
                                size_t *size_old) {}

#endif

extern "C" void *__tsan_get_current_fiber();
extern "C" void *__tsan_create_fiber(unsigned flags);
extern "C" void __tsan_destroy_fiber(void *fiber);
extern "C" void __tsan_switch_to_fiber(void *fiber, unsigned flags);

#if !REN_TSAN

extern "C" ALWAYS_INLINE void *__tsan_get_current_fiber() { return nullptr; }
extern "C" ALWAYS_INLINE void *__tsan_create_fiber(unsigned flags) {
  return nullptr;
}
extern "C" ALWAYS_INLINE void __tsan_destroy_fiber(void *fiber) {}
extern "C" ALWAYS_INLINE void __tsan_switch_to_fiber(void *fiber,
                                                     unsigned flags) {}

#endif

// x86_64 System V ABI.
struct FiberContextSystemV {
  void (*rip)();
  void *rsp;
  u64 rbx;
  u64 rbp;
  u64 r12;
  u64 r13;
  u64 r14;
  u64 r15;
  void *stack_bottom;
  usize stack_size;
  void *tsan;
  const char *label;
};

extern "C" void fiber_save_context_system_v(FiberContextSystemV *context);

extern "C" void fiber_load_context_system_v(const FiberContextSystemV *context);

extern "C" void
fiber_switch_context_system_v(FiberContextSystemV *this_context,
                              const FiberContextSystemV *other_context);

// x64 ABI.
struct FiberContext_x64 {
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
  alignas(16) u64 xmm6[2];
  alignas(16) u64 xmm7[2];
  alignas(16) u64 xmm8[2];
  alignas(16) u64 xmm9[2];
  alignas(16) u64 xmm10[2];
  alignas(16) u64 xmm11[2];
  alignas(16) u64 xmm12[2];
  alignas(16) u64 xmm13[2];
  alignas(16) u64 xmm14[2];
  alignas(16) u64 xmm15[2];
  void *stack_bottom;
  usize stack_size;
  void *tsan;
  const char *label;
};

extern "C" void fiber_save_context_x64(FiberContext_x64 *context);

extern "C" void fiber_load_context_x64(const FiberContext_x64 *context);

extern "C" void fiber_switch_context_x64(FiberContext_x64 *this_context,
                                         const FiberContext_x64 *other_context);

#if __linux__
using FiberContext = FiberContextSystemV;
#define platform_fiber_save_context fiber_save_context_system_v
#define platform_fiber_load_context fiber_load_context_system_v
#define platform_fiber_switch_context fiber_switch_context_system_v
#endif

#if _WIN32
using FiberContext = FiberContext_x64;
#define platform_fiber_save_context fiber_save_context_x64
#define platform_fiber_load_context fiber_load_context_x64
#define platform_fiber_switch_context fiber_switch_context_x64
#endif

ALWAYS_INLINE void fiber_load_context(const FiberContext &context) {
  if (context.label) {
    TracyFiberEnter(context.label);
  } else {
    TracyFiberLeave;
  }
  __tsan_switch_to_fiber(context.tsan, 0);
  // Target context must be passed by reference since ASAN thinks that the stack
  // is not longer valid after this call.
  __sanitizer_start_switch_fiber(nullptr, context.stack_bottom,
                                 context.stack_size);
  std::atomic_signal_fence(std::memory_order_release);
  platform_fiber_load_context(&context);
}

ALWAYS_INLINE void fiber_switch_context(NotNull<FiberContext *> this_context,
                                        const FiberContext &other_context) {
  ren_assert(this_context != &other_context);
  if (other_context.label) {
    TracyFiberEnter(other_context.label);
  } else {
    TracyFiberLeave;
  }
  __tsan_switch_to_fiber(other_context.tsan, 0);
  void *fake_stack;
  __sanitizer_start_switch_fiber(&fake_stack, other_context.stack_bottom,
                                 other_context.stack_size);
  std::atomic_signal_fence(std::memory_order_release);
  platform_fiber_switch_context(this_context, &other_context);
  std::atomic_signal_fence(std::memory_order_acquire);
  __sanitizer_finish_switch_fiber(fake_stack, nullptr, nullptr);
}

inline void fiber_start() {
  std::atomic_signal_fence(std::memory_order_acquire);
  __sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr);
}

inline void fiber_panic() {
  std::fputs("Tried to return from fiber\n", stderr);
  std::abort();
};

[[nodiscard]] inline FiberContext fiber_init_context(void (*fiber_main)(),
                                                     void *stack, usize size,
                                                     const char *label) {
  u8 *sp = (u8 *)stack + size;

  // Push return address.
  sp -= 8;
  *(void (**)())sp = fiber_panic;

  // Call: push return address to fiber main.
  // Don't align stack because it needs to be aligned before ret.
  sp -= 8;
  *(void (**)())sp = fiber_main;

  return {
      .rip = fiber_start,
      .rsp = sp,
      .stack_bottom = (u8 *)stack + size,
      .stack_size = size,
      .tsan = __tsan_create_fiber(0),
      .label = label,
  };
}

#if __linux__

[[nodiscard]] ALWAYS_INLINE FiberContext fiber_thread_context() {
  FiberContext fiber = {
      .tsan = __tsan_create_fiber(0),
  };
  __tsan_switch_to_fiber(fiber.tsan, 0);
  pthread_attr_t attr;
  pthread_getattr_np(pthread_self(), &attr);
  pthread_attr_getstack(&attr, &fiber.stack_bottom, &fiber.stack_size);
  pthread_attr_destroy(&attr);
  return fiber;
}

#endif

inline void fiber_destroy_context(NotNull<FiberContext *> fiber) {
  __tsan_destroy_fiber(fiber->tsan);
  *fiber = {};
}

} // namespace ren
