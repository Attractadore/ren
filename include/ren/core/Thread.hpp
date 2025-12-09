#pragma once
#include "Span.hpp"
#include "StdDef.hpp"

namespace ren {

struct Processor {
  u32 cpu = 0;
  u32 core = 0;
  u32 numa = 0;
};

Span<Processor> cpu_topology(NotNull<Arena *> arena);

usize thread_min_stack_size();

struct Thread {
  void *m_handle = nullptr;
};

struct ThreadDesc {
  const char *name = nullptr;
  void (*proc)(void *) = nullptr;
  void *param = nullptr;
  usize stack_size = 0;
  Span<const u32> affinity;
};

Thread thread_create(const ThreadDesc &desc);

[[noreturn]] void thread_exit(int code);

int thread_join(Thread thread);

bool is_main_thread();

} // namespace ren
