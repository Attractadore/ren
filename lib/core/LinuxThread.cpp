#if __linux__
#include "Posix.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Array.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Futex.hpp"
#include "ren/core/Thread.hpp"

#include <atomic>
#include <common/TracySystem.hpp>
#include <fcntl.h>
#include <fmt/base.h>
#include <numa.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

namespace ren {

namespace {

pthread_t thread_pthread(Thread thread) { return (pthread_t)thread.m_handle; }

struct PosixThreadParam {
  int *launched = nullptr;
  const ThreadDesc *desc = nullptr;
};

void *posix_thread_start(void *void_param) {
  auto *param = (const PosixThreadParam *)void_param;
  ThreadDesc desc = *param->desc;

  std::atomic_ref(*param->launched).store(true, std::memory_order_release);
  futex_wake_one(param->launched);

  tracy::SetThreadName(desc.name);

  desc.proc(desc.param);

  return (void *)(uintptr_t)(EXIT_SUCCESS);
}

} // namespace

Span<Processor> cpu_topology(NotNull<Arena *> arena) {
  if (numa_available() == -1) {
    fmt::println(stderr, "libnuma is not available");
    exit(EXIT_FAILURE);
  }

  const struct bitmask *cpus = numa_all_cpus_ptr;
  usize num_cpus = numa_num_possible_cpus();

  ScratchArena scratch;
  DynamicArray<Processor> processors;
  for (usize cpu : range(num_cpus)) {
    if (not numa_bitmask_isbitset(cpus, cpu)) {
      continue;
    }

    Processor processor = {};
    processor.cpu = cpu;

    char core_buffer[128];
    char path[256];
    usize len =
        fmt::format_to_n(path, sizeof(path),
                         "/sys/devices/system/cpu/cpu{}/topology/core_id", cpu)
            .size;
    ren_assert(len + 1 <= sizeof(path));
    path[len] = 0;
    IoResult<File> file = open(Path::init(path), FileAccessMode::ReadOnly);
    if (!file) {
      fmt::println(stderr, "thread: failed to open {}: {}", path, file.error());
      exit(EXIT_FAILURE);
    }
    if (IoResult<usize> result = read(*file, core_buffer, sizeof(core_buffer));
        !result) {
      fmt::println(stderr, "thread: failed to read {}: {}", path,
                   result.error());
      exit(EXIT_FAILURE);
    }
    close(*file);
    file = {};

    int cnt = std::sscanf(core_buffer, "%u", &processor.core);
    ren_assert(cnt == 1);

    processor.numa = numa_node_of_cpu(cpu);

    processors.push(scratch, processor);
  }

  return Span(processors).copy(arena);
}

usize thread_min_stack_size() { return PTHREAD_STACK_MIN; }

Thread thread_create(const ThreadDesc &desc) {
  pthread_attr_t attr;
  POSIX_CHECK(pthread_attr_init(&attr));

  sigset_t signal_mask;
  POSIX_CHECK(sigfillset(&signal_mask));
  POSIX_CHECK(pthread_attr_setsigmask_np(&attr, &signal_mask));

  if (desc.affinity.m_size > 0) {
    u32 num_cpus = 0;
    for (u32 cpu : desc.affinity) {
      num_cpus = max(num_cpus, cpu + 1);
    }
    cpu_set_t *cpu_mask = CPU_ALLOC(num_cpus);
    for (u32 cpu : desc.affinity) {
      CPU_SET_S(cpu, num_cpus, cpu_mask);
    }
    POSIX_CHECK(pthread_attr_setaffinity_np(&attr, num_cpus, cpu_mask));
    CPU_FREE(cpu_mask);
  }

  if (desc.stack_size) {
    POSIX_CHECK(pthread_attr_setstacksize(&attr, desc.stack_size));
  }

  int launched = false;
  PosixThreadParam param = {
      .launched = &launched,
      .desc = &desc,
  };
  pthread_t thread;
  POSIX_CHECK(pthread_create(&thread, &attr, posix_thread_start, &param));
  POSIX_CHECK(pthread_attr_destroy(&attr));

wait:
  if (not std::atomic_ref(launched).load(std::memory_order_acquire)) {
    futex_wait(&launched, false);
    goto wait;
  }

  return {(void *)thread};
}

void thread_exit(int code) { pthread_exit((void *)(uintptr_t)code); }

int thread_join(Thread thread) {
  pthread_t handle = thread_pthread(thread);
  void *ret;
  POSIX_CHECK(pthread_join(handle, &ret));
  return (uintptr_t)ret;
}

static const pthread_t MAIN_THREAD = pthread_self();
bool is_main_thread() { return pthread_self() == MAIN_THREAD; }

} // namespace ren
#endif
