// TODO: Win32 support.
#if __linux__
#include "ren/core/Job.hpp"
#include "ren/core/Fiber.hpp"
#include "ren/core/Mutex.hpp"
#include "ren/core/Queue.hpp"

#include <pthread.h>
#include <signal.h>
#include <tracy/Tracy.hpp>

namespace ren {

namespace {

template <typename T> T *free_list_atomic_pop(T **free_list) {
  std::atomic_ref<T *> ref(*free_list);
  T *head = ref.load(std::memory_order_relaxed);
  while (head) {
    bool success = ref.compare_exchange_weak(
        head, head->next, std::memory_order_relaxed, std::memory_order_relaxed);
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
    bool success = ref.compare_exchange_weak(
        head, node, std::memory_order_relaxed, std::memory_order_relaxed);
    if (success) {
      return;
    }
  }
}

} // namespace

Job *job_tls_running_job();
void job_tls_set_running_job(Job *job);

FiberContext *job_tls_scheduler_fiber();

struct alignas(FIBER_STACK_ALIGNMENT) StackFreeListNode {
  StackFreeListNode *next = nullptr;
};

enum class JobState {
  Running,
  Suspended,
  Resumed,
};

struct alignas(CACHE_LINE_SIZE) JobAtomicCounter {
  u32 value = 0;
  JobAtomicCounter *next = nullptr;
  JobState parent_job_state = JobState::Running;
};

struct alignas(CACHE_LINE_SIZE) Job {
  union {
    Job *next = nullptr;
    Job *parent;
  };
  JobPriority priority = {};
  bool is_main_job = false;
  FiberContext context = {};
  JobFunction *function = nullptr;
  void *payload = nullptr;
  StackFreeListNode *stack = nullptr;
  JobAtomicCounter *counter = nullptr;
  JobAtomicCounter *child_counters = nullptr;
};

struct alignas(CACHE_LINE_SIZE) QueuedJob {
  Job *job = nullptr;
};

struct JobServer {
  Mutex m_arena_mutex;

  Arena m_arena;
  Span<pthread_t> m_workers;
  MpMcQueue<QueuedJob> m_high_priority;
  QueuedJob m_main_priority;
  MpMcQueue<QueuedJob> m_normal_priority;

  usize m_page_size = 0;
  StackFreeListNode *m_stack_free_list = nullptr;
  Job *m_job_free_list = nullptr;
  JobAtomicCounter *m_atomic_counter_free_list = nullptr;
};

static JobServer job_server;

static thread_local bool job_is_main_thread = false;

static Job *job_schedule() {
  ZoneScoped;
  Optional<QueuedJob> high_priority = job_server.m_high_priority.try_pop();
  if (high_priority) {
    return high_priority->job;
  }
  if (job_is_main_thread) {
    // Sync with enqueue.
    Job *main_priority = std::atomic_ref(job_server.m_main_priority.job)
                             .exchange(nullptr, std::memory_order_acquire);
    if (main_priority) {
      return main_priority;
    }
  }
  QueuedJob normal_priority = job_server.m_normal_priority.pop();
  return normal_priority.job;
}

static void job_enqueue(Job *job) {
  ZoneScoped;
  if (job->priority == JobPriority::High) {
    job_server.m_high_priority.push({job});
  } else if (job->is_main_job) {
    // Sync with scheduler.
    std::atomic_ref(job_server.m_main_priority.job)
        .store(job, std::memory_order_release);
  } else {
    ren_assert(job->priority == JobPriority::Normal);
    job_server.m_normal_priority.push({job});
  }
}

static void job_free(Job *job) {
  ZoneScoped;

  bool all_done = 1 == std::atomic_ref(job->counter->value)
                           .fetch_add(-1, std::memory_order_relaxed);
  if (all_done) {
    Job *parent = job->parent;
    ren_assert(parent);

    std::atomic_ref parent_job_state(job->counter->parent_job_state);

    JobState state = parent_job_state.exchange(JobState::Resumed,
                                               // Sync with suspend.
                                               std::memory_order_acq_rel);
    if (state == JobState::Suspended) {
      job_enqueue(parent);
    }
  }

  free_list_atomic_push(&job_server.m_stack_free_list, job->stack);
  JobAtomicCounter *counter = job->child_counters;
  while (counter) {
    JobAtomicCounter *next = counter->next;
    free_list_atomic_push(&job_server.m_atomic_counter_free_list, counter);
    counter = next;
  }
  free_list_atomic_push(&job_server.m_job_free_list, job);
  // Sync with free list pop.
  std::atomic_thread_fence(std::memory_order_release);
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

static const usize JOB_SCHEDULER_STACK_SIZE = PTHREAD_STACK_MIN;

static void job_server_scheduler_fiber() {
  while (true) {
    Job *next = job_schedule();
    job_tls_set_running_job(next);
    fiber_switch_context(job_tls_scheduler_fiber(), next->context);
    Job *job = job_tls_running_job();
    job_free(job);
  }
}

static void *job_server_worker(void *) {
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
  job_server_scheduler_fiber();
  return nullptr;
}

static void job_wait_for_counter(Job *job, JobAtomicCounter *counter) {
  if (std::atomic_ref(counter->value).load(std::memory_order_relaxed) == 0) {
    return;
  }

  fiber_save_context(&job->context);

  std::atomic_ref job_state(counter->parent_job_state);

  JobState state = JobState::Running;
  job_state.compare_exchange_strong(state, JobState::Suspended,
                                    // Sync with future resume.
                                    std::memory_order_release,
                                    // Sync with already executed resume.
                                    std::memory_order_acquire);
  if (state != JobState::Running) {
    ren_assert(state == JobState::Resumed);
    return;
  }

  Job *next = job_schedule();
  job_tls_set_running_job(next);
  fiber_switch_context(&job->context, next->context);
}

static void job_wait_for_children(Job *job) {
  JobAtomicCounter *counter = job->child_counters;
  while (counter) {
    job_wait_for_counter(job, counter);
    counter = counter->next;
  }
}

static void job_fiber_main() {
  Job *job = job_tls_running_job();
  ren_assert(job);
  ren_assert(job->function);
  job->function(job->payload);
  job_wait_for_children(job);
  fiber_load_context(*job_tls_scheduler_fiber());
}

void launch_job_server() {
  job_server = {
      .m_arena = Arena::init(),
      .m_high_priority = MpMcQueue<QueuedJob>::init(),
      .m_normal_priority = MpMcQueue<QueuedJob>::init(),
      .m_page_size = vm_page_size(),
  };
  // TODO: retrieve the number of cores.
  usize num_cores = 8;
  job_server.m_workers =
      Span<pthread_t>::allocate(&job_server.m_arena, num_cores);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  sigset_t signal_mask;
  sigemptyset(&signal_mask);
  pthread_attr_setsigmask_np(&attr, &signal_mask);
  for (pthread_t &worker : job_server.m_workers) {
    // TODO: set thread affinity.
    void *stack = job_allocate_stack(JOB_SCHEDULER_STACK_SIZE);
    pthread_attr_setstack(&attr, stack, JOB_SCHEDULER_STACK_SIZE);
    pthread_create(&worker, &attr, job_server_worker, nullptr);
  }
  pthread_attr_destroy(&attr);
  // At this points all workers haven't touched any
  // shared resources and have possibly gone to sleep. So no need to lock.
  job_is_main_thread = true;
  Job *main_job = job_server.m_arena.allocate<Job>();
  *main_job = {.is_main_job = true};
  job_enqueue(main_job);
  void *stack = job_allocate_stack(JOB_SCHEDULER_STACK_SIZE);
  FiberContext *scheduler = job_tls_scheduler_fiber();
  *scheduler = {
      .rip = job_server_scheduler_fiber,
      .rsp = (u8 *)stack + JOB_SCHEDULER_STACK_SIZE,
  };
  // Initialize scheduler.
  fiber_switch_context(&main_job->context, *scheduler);
}

void stop_job_server() {
  // Sync with enqueue.
  Job *main_job = std::atomic_ref(job_server.m_main_priority.job)
                      .load(std::memory_order_acquire);
  ren_assert_msg(!main_job, "Job server must be stopped from the main thread");
  main_job = job_tls_running_job();
  job_wait_for_children(main_job);
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

  JobAtomicCounter *counter =
      free_list_atomic_pop(&job_server.m_atomic_counter_free_list);
  [[unlikely]] if (!counter) {
    job_server.m_arena_mutex.lock();
    counter = job_server.m_arena.allocate<JobAtomicCounter>();
    job_server.m_arena_mutex.unlock();
  }
  // Sync with free list push.
  std::atomic_thread_fence(std::memory_order_acquire);

  Job *parent = job_tls_running_job();
  ren_assert(parent);
  *counter = {.value = (u32)jobs.m_size, .next = parent->child_counters};
  parent->child_counters = counter;

  for (JobDesc job_desc : jobs) {
    Job *job = free_list_atomic_pop(&job_server.m_job_free_list);
    [[unlikely]] if (!job) {
      job_server.m_arena_mutex.lock();
      job = job_server.m_arena.allocate<Job>();
      job_server.m_arena_mutex.unlock();
    }

    usize stack_size = 32 * KiB;
    StackFreeListNode *stack =
        free_list_atomic_pop(&job_server.m_stack_free_list);
    [[unlikely]] if (!stack) {
      // TODO: add option to set stack size, or set stack size based on amount
      // of stack space used by previous jobs with the same function pointer.
      stack = job_allocate_stack(stack_size);
    }

    // Sync with free list push.
    std::atomic_thread_fence(std::memory_order_acquire);

    *job = {
        .parent = parent,
        .priority = parent->priority == JobPriority::High ? JobPriority::High
                                                          : job_desc.priority,
        .context =
            {
                .rip = job_fiber_main,
                .rsp = (u8 *)stack + stack_size,
            },
        .function = job_desc.function,
        .payload = job_desc.payload,
        .stack = stack,
        .counter = counter,
    };
    job_enqueue(job);
  }

  return {counter};
}

void job_wait(JobToken token) {
  Job *job = job_tls_running_job();
  job_wait_for_counter(job, token.counter);
  // TODO: return counter to pool after waiting.
}

bool job_is_done(JobToken token) {
  return std::atomic_ref(token.counter->value)
             .load(std::memory_order_relaxed) == 0;
  // TODO: return counter to pool.
}

} // namespace ren
#endif
