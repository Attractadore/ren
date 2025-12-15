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

i32 read_int(Path path) {
  ScratchArena scratch;

  errno = 0;
  int fd = ::open(path.m_str.zero_terminated(scratch), O_RDONLY);
  if (fd == -1) {
    fmt::println(stderr, "Failed to open {}: {}", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  char buffer[2048];
  errno = 0;
  ssize_t num_read = ::read(fd, buffer, sizeof(buffer) - 1);
  if (num_read < 0) {
    fmt::println(stderr, "Failed to read {}: {}", path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  buffer[num_read] = 0;

  int value = -1;
  if (std::sscanf(buffer, "%d", &value) != 1) {
    fmt::println(stderr, "Failed to parse {}: \"{}\"", path, buffer);
    exit(EXIT_FAILURE);
  }

  return value;
}

} // namespace

Span<Processor> cpu_topology(NotNull<Arena *> arena) {
  ren_assert(is_main_thread());
  ScratchArena scratch;

  i32 num_cpus = 1;
  cpu_set_t *cpus = nullptr;
retry:
  cpus = CPU_ALLOC(num_cpus);
  errno = 0;
  pthread_getaffinity_np(pthread_self(), CPU_ALLOC_SIZE(num_cpus), cpus);
  if (errno == EINVAL) {
    CPU_FREE(cpus);
    num_cpus *= 2;
    goto retry;
  }
  if (errno) {
    fmt::println(stderr, "Failed to get main thread affinity: {}",
                 strerror(errno));
    exit(EXIT_FAILURE);
  }
  num_cpus = CPU_COUNT_S(CPU_ALLOC_SIZE(num_cpus), cpus);

  DynamicArray<Processor> processors;
  for (usize cpu : range(num_cpus)) {
    if (not CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(num_cpus), cpus)) {
      continue;
    }
    processors.push(
        scratch,
        {
            .cpu = (u32)cpu,
            .core = (u32)read_int(Path::init(
                format(scratch,
                       "/sys/devices/system/cpu/cpu{}/topology/core_id", cpu))),
            .numa = (u32)read_int(Path::init(format(
                scratch,
                "/sys/devices/system/cpu/cpu{}/topology/physical_package_id",
                cpu))),
        });
  }
  CPU_FREE(cpus);

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
