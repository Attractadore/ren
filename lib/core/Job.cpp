// TODO: Win32 support.
#if __linux__
#include "ren/core/Job.hpp"
#include "ren/core/Fiber.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Futex.hpp"
#include "ren/core/Mutex.hpp"
#include "ren/core/Queue.hpp"
#include "ren/core/Thread.hpp"

#include <common/TracySystem.hpp>
#include <pthread.h>
#include <signal.h>
#include <tracy/Tracy.hpp>

namespace ren {

namespace {

template <typename T> void list_init(T *node) {
  node->next = node;
  node->prev = node;
}

template <typename T> void list_insert_after(T *list, T *node) {
  T *next = list->next;

  node->next = next;
  node->prev = list;

  list->next = node;
  next->prev = node;
}

template <typename T> void list_remove(T *node) {
  T *next = node->next;
  T *prev = node->prev;
  prev->next = next;
  next->prev = prev;
  node->next = nullptr;
  node->prev = nullptr;
}

template <typename T> T *free_list_atomic_pop(T **free_list) {
  std::atomic_ref<T *> ref(*free_list);
  // Sync with free list push.
  T *head = ref.load(std::memory_order_acquire);
  while (head) {
    T *next = head->next;
    // Sync with free list push.
    bool success = ref.compare_exchange_weak(
        head, next, std::memory_order_acquire, std::memory_order_acquire);
    if (success) {
      return head;
    }
  }
  return nullptr;
}

template <typename T> void free_list_atomic_push(T **free_list, T *node) {
  std::atomic_ref<T *> ref(*free_list);
  T *head = ref.load(std::memory_order_relaxed);
  while (true) {
    node->next = head;
    bool success = ref.compare_exchange_weak(head, node,
                                             // Sync with free list pop.
                                             std::memory_order_release,
                                             std::memory_order_relaxed);
    if (success) {
      return;
    }
  }
}

} // namespace

Job *job_tls_running_job();
void job_tls_set_running_job(Job *job);

FiberContext *job_tls_scheduler_fiber();

static thread_local bool job_is_main_thread = false;

struct alignas(FIBER_STACK_ALIGNMENT) StackFreeListNode {
  StackFreeListNode *next = nullptr;
};

enum class JobState {
  Running,
  Suspended,
  Resumed,
};

struct alignas(CACHE_LINE_SIZE) JobAtomicCounter {
  JobAtomicCounter *next = nullptr;
  JobAtomicCounter *prev = nullptr;
  u32 value = 0;
  JobState job_state = JobState::Running;
  u64 generation = 0;
};

struct alignas(CACHE_LINE_SIZE) Job {
  // Read-only data.
  union {
    Job *next = nullptr;
    Job *parent;
  };
  JobPriority priority = {};
  bool is_main_job = false;
  JobFunction *function = nullptr;
  void *payload = nullptr;
  JobAtomicCounter *counter = nullptr;

  alignas(CACHE_LINE_SIZE) FiberContext context = {};
  alignas(CACHE_LINE_SIZE) JobAtomicCounter list_of_counters = {};
};

struct alignas(CACHE_LINE_SIZE) QueuedJob {
  Job *job = nullptr;
};

struct JobServer {
  // Read-only data.
  usize m_page_size = 0;
  Span<pthread_t> m_workers;

  // Arena mutex.
  alignas(CACHE_LINE_SIZE) Mutex m_arena_mutex;
  // Arena data.
  alignas(CACHE_LINE_SIZE) Arena m_arena;

  // Scheduler mutex.
  alignas(CACHE_LINE_SIZE) Mutex m_scheduler_mutex;
  // Scheduler data.
  alignas(CACHE_LINE_SIZE) int m_num_enqueued = 0;
  Queue<QueuedJob> m_high_priority;
  Queue<QueuedJob> m_normal_priority;

  alignas(CACHE_LINE_SIZE) Job *m_main_job = nullptr;
  int m_main_job_ready = false;

  // Free lists.
  alignas(CACHE_LINE_SIZE) StackFreeListNode *m_stack_free_list = nullptr;
  alignas(CACHE_LINE_SIZE) Job *m_job_free_list = nullptr;
  alignas(CACHE_LINE_SIZE)
      JobAtomicCounter *m_atomic_counter_free_list = nullptr;
};

static JobServer job_server;

static Job *job_schedule() {
  if (job_is_main_thread) {
    ZoneScopedN("Schedule main job");
    while (true) {
      int ready = std::atomic_ref(job_server.m_main_job_ready)
                      .exchange(false, std::memory_order_acquire);
      if (ready) {
        return job_server.m_main_job;
      }
      futex_wait(&job_server.m_main_job_ready, false);
    }
  }

  ZoneScopedN("Schedule worker job");
retry:
  job_server.m_scheduler_mutex.lock();
  [[unlikely]] if (job_server.m_num_enqueued == 0) {
    job_server.m_scheduler_mutex.unlock();
    futex_wait(&job_server.m_num_enqueued, 0);
    goto retry;
  }

  Job *job = []() {
    Optional<QueuedJob> high_priority = job_server.m_high_priority.try_pop();
    if (high_priority) {
      return high_priority->job;
    }
    Optional<QueuedJob> normal_priority =
        job_server.m_normal_priority.try_pop();
    ren_assert(normal_priority);
    return normal_priority->job;
  }();
  job_server.m_num_enqueued--;
  job_server.m_scheduler_mutex.unlock();

  return job;
}

static void job_enqueue(Job *job) {
  ZoneScoped;

  if (job->is_main_job) {
    std::atomic_ref(job_server.m_main_job_ready)
        .store(true, std::memory_order_release);
    futex_wake_one(&job_server.m_main_job_ready);
    return;
  }

  job_server.m_scheduler_mutex.lock();

  if (job->priority == JobPriority::High) {
    job_server.m_high_priority.push({job});
  } else {
    ren_assert(job->priority == JobPriority::Normal);
    job_server.m_normal_priority.push({job});
  }

  [[unlikely]] if (job_server.m_num_enqueued++ == 0) {
    futex_wake_one(&job_server.m_num_enqueued);
  }

  job_server.m_scheduler_mutex.unlock();
}

static void job_free_atomic_counter(JobAtomicCounter *counter) {
  list_remove(counter);
  // Can be read by previous owner.
  std::atomic_ref(counter->generation).fetch_add(1, std::memory_order_relaxed);
  free_list_atomic_push(&job_server.m_atomic_counter_free_list, counter);
}

static void job_free(Job *job) {
  ZoneScoped;
  ren_assert(not job->is_main_job);

  bool all_done = 1 == std::atomic_ref(job->counter->value)
                           .fetch_sub(1, std::memory_order_relaxed);
  if (all_done) {
    Job *parent = job->parent;
    ren_assert(parent);

    JobState state = std::atomic_ref(job->counter->job_state)
                         .exchange(JobState::Resumed,
                                   // Sync with suspend.
                                   std::memory_order_acq_rel);
    if (state == JobState::Suspended) {
      job_enqueue(parent);
    }
  }

  auto *stack = (StackFreeListNode *)((u8 *)job->context.stack_bottom -
                                      job->context.stack_size);
  free_list_atomic_push(&job_server.m_stack_free_list, stack);
  fiber_destroy_context(&job->context);
  free_list_atomic_push(&job_server.m_job_free_list, job);
}

static StackFreeListNode *job_allocate_stack(usize stack_size) {
  stack_size =
      (stack_size + job_server.m_page_size - 1) & ~(job_server.m_page_size - 1);
  usize guard_size = job_server.m_page_size;
  u8 *new_stack = (u8 *)vm_allocate(guard_size + stack_size + guard_size);
  new_stack += guard_size;
  vm_protect(new_stack - guard_size, guard_size, PagePermissionNone);
  vm_commit(new_stack, stack_size);
  vm_protect(new_stack + stack_size, guard_size, PagePermissionNone);
  return (StackFreeListNode *)new_stack;
}

#if !REN_TSAN
static const usize JOB_WORKER_STACK_SIZE = PTHREAD_STACK_MIN;
static const usize JOB_STACK_SIZE = 32 * KiB;
#else
static const usize JOB_WORKER_STACK_SIZE = 256 * KiB;
static const usize JOB_STACK_SIZE = 256 * KiB;
#endif

static void *job_server_worker(void *name) {
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
  tracy::SetThreadName((const char *)name);
  FiberContext *scheduler = job_tls_scheduler_fiber();
  *scheduler = fiber_thread_context();
  while (true) {
    Job *next = job_schedule();
    job_tls_set_running_job(next);
    fiber_switch_context(scheduler, next->context);
    Job *job = job_tls_running_job();
    job_free(job);
  }
  return nullptr;
}

static void job_wait_for_counter(Job *job, JobAtomicCounter *counter) {
  // TODO: what memory order should this be?
  if (std::atomic_ref(counter->value).load(std::memory_order_relaxed) == 0) {
    return;
  }

  JobState state = JobState::Running;
  std::atomic_ref(counter->job_state)
      .compare_exchange_strong(state, JobState::Suspended,
                               // Sync with future resume.
                               std::memory_order_release,
                               // Sync with already executed resume.
                               std::memory_order_acquire);
  if (state != JobState::Running) {
    ren_assert(state == JobState::Resumed);
    return;
  }

  Job *next = job_schedule();
  if (job != next) {
    job_tls_set_running_job(next);
    fiber_switch_context(&job->context, next->context);
  }
}

static void job_fiber_main() {
  {
    ZoneScoped;
    Job *job = job_tls_running_job();
    ZoneText(job->context.label, std::strlen(job->context.label));
    ren_assert(job);
    ren_assert(job->function);
    job->function(job->payload);
    ren_assert(job == job_tls_running_job());
    JobAtomicCounter *counter = job->list_of_counters.next;
    while (counter != &job->list_of_counters) {
      JobAtomicCounter *next = counter->next;
      job_wait_for_counter(job, counter);
      job_free_atomic_counter(counter);
      counter = next;
    }
  }
  fiber_load_context(*job_tls_scheduler_fiber());
}

void launch_job_server() {
  ScratchArena scratch;

  job_is_main_thread = true;
  job_server = {
      .m_page_size = vm_page_size(),
      .m_arena = Arena::init(),
      .m_high_priority = Queue<QueuedJob>::init(),
      .m_normal_priority = Queue<QueuedJob>::init(),
  };

  auto topology = cpu_topology(scratch);
  u32 num_cpus = 0;
  u32 max_num_cores = 0;
  for (Processor processor : topology) {
    num_cpus = max(num_cpus, processor.id + 1);
    max_num_cores = max(max_num_cores, processor.core_id + 1);
  }
  Span<bool> core_mask = Span<bool>::allocate(scratch, max_num_cores);
  u32 package = topology[0].package_id;
  u32 cluster = topology[0].cluster_id;
  for (Processor processor : topology) {
    core_mask[processor.core_id] =
        processor.package_id == package and processor.cluster_id == cluster;
  }
  u32 num_cores = 0;
  for (bool core_bit : core_mask) {
    if (core_bit) {
      num_cores++;
    }
  }
  Span<u32> cores = Span<u32>::allocate(scratch, num_cores);
  u32 core_offset = 0;
  for (u32 core_id : range(max_num_cores)) {
    if (core_mask[core_id]) {
      cores[core_offset++] = core_id;
    }
  }
  fmt::println("job_server: Found {} cores", num_cores);

  job_server.m_workers =
      Span<pthread_t>::allocate(&job_server.m_arena, num_cores);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  sigset_t signal_mask;
  sigemptyset(&signal_mask);
  pthread_attr_setsigmask_np(&attr, &signal_mask);
  cpu_set_t *cpu_mask = CPU_ALLOC(num_cpus);
  for (usize i : range(num_cores)) {
    ScratchArena scratch;

    String8 name =
        format(scratch, "Job server worker {} on core {}", i, cores[i]);
    fmt::println("job_server: Run worker {} on core {}", i, cores[i]);

    pthread_attr_setstacksize(&attr, JOB_WORKER_STACK_SIZE);

    CPU_ZERO_S(num_cpus, cpu_mask);
    for (Processor processor : topology) {
      if (processor.cluster_id == cluster and
          processor.package_id == package and processor.core_id == cores[i]) {
        CPU_SET_S(processor.id, num_cpus, cpu_mask);
      }
    }
    pthread_attr_setaffinity_np(&attr, num_cpus, cpu_mask);

    pthread_create(&job_server.m_workers[i], &attr, job_server_worker,
                   (void *)name.zero_terminated(&job_server.m_arena));
  }
  pthread_attr_destroy(&attr);
  CPU_FREE(cpu_mask);

  // At this points all workers haven't touched any
  // shared resources and have possibly gone to sleep. So no need to lock.
  job_server.m_main_job = job_server.m_arena.allocate<Job>();
  *job_server.m_main_job = {
      .is_main_job = true,
      .context = fiber_thread_context(),
  };
  list_init(&job_server.m_main_job->list_of_counters);
  job_tls_set_running_job(job_server.m_main_job);
}

void stop_job_server() {
  // Sync with enqueue.
  Job *main_job = job_tls_running_job();
  ren_assert_msg(main_job == job_server.m_main_job,
                 "Job server must be stopped from the main thread");
  JobAtomicCounter *counter = main_job->list_of_counters.next;
  while (counter != &main_job->list_of_counters) {
    JobAtomicCounter *next = counter->next;
    job_wait_for_counter(main_job, counter);
    counter = next;
  }
  for (pthread_t worker : job_server.m_workers) {
    pthread_cancel(worker);
  }
  for (pthread_t worker : job_server.m_workers) {
    void *worker_exit_status;
    pthread_join(worker, &worker_exit_status);
    ren_assert(worker_exit_status == PTHREAD_CANCELED);
  }
  // Zero and leak memory.
  job_server = {};
}

JobToken job_dispatch(Span<const JobDesc> jobs) {
  ZoneScoped;

  Job *parent = job_tls_running_job();
  ren_assert(parent);

  JobAtomicCounter *counter =
      free_list_atomic_pop(&job_server.m_atomic_counter_free_list);
  [[unlikely]] if (!counter) {
    job_server.m_arena_mutex.lock();
    counter = job_server.m_arena.allocate<JobAtomicCounter>();
    job_server.m_arena_mutex.unlock();
  }
  counter->value = 0;
  counter->job_state = JobState::Running;
  list_insert_after(&parent->list_of_counters, counter);

  for (JobDesc job_desc : jobs) {
    Job *job = free_list_atomic_pop(&job_server.m_job_free_list);
    [[unlikely]] if (!job) {
      job_server.m_arena_mutex.lock();
      job = job_server.m_arena.allocate<Job>();
      job_server.m_arena_mutex.unlock();
    }

    usize stack_size = JOB_STACK_SIZE;
    StackFreeListNode *stack =
        free_list_atomic_pop(&job_server.m_stack_free_list);
    [[unlikely]] if (!stack) {
      // TODO: add option to set stack size, or set stack size based on
      // amount of stack space used by previous jobs with the same function
      // pointer.
      stack = job_allocate_stack(stack_size);
    }

    *job = {
        .parent = parent,
        .priority = parent->priority == JobPriority::High ? JobPriority::High
                                                          : job_desc.priority,
        .function = job_desc.function,
        .payload = job_desc.payload,
        .counter = counter,
        .context =
            fiber_init_context(job_fiber_main, stack, stack_size,
                               job_desc.label ? job_desc.label : "Untitled"),
    };
    list_init(&job->list_of_counters);
    job_enqueue(job);
  }

  return {counter, counter->generation};
}

void job_wait(JobToken token) {
  // Might be in use on another thread if it has been freed.
  u64 generation = std::atomic_ref(token.counter->generation)
                       // TODO: what memory order should this be?
                       .load(std::memory_order_acquire);
  if (generation != token.generation) {
    return;
  }
  Job *job = job_tls_running_job();
  job_wait_for_counter(job, token.counter);
  job_free_atomic_counter(token.counter);
}

bool job_is_done(JobToken token) {
  // Might be in use on another thread if it has been freed.
  u64 generation = std::atomic_ref(token.counter->generation)
                       // TODO: what memory order should this be?
                       .load(std::memory_order_acquire);
  if (generation != token.generation) {
    return true;
  }
  // TODO: what memory order should this be?
  bool done =
      0 ==
      std::atomic_ref(token.counter->value).load(std::memory_order_relaxed);
  if (done) {
    job_free_atomic_counter(token.counter);
    return true;
  }
  return false;
}

} // namespace ren
#endif
