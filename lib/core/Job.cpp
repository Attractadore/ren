#include "ren/core/Job.hpp"
#include "Job.hpp"
#include "ren/core/BlockAllocator.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/Futex.hpp"
#include "ren/core/Mutex.hpp"
#include "ren/core/Queue.hpp"
#include "ren/core/Thread.hpp"
#include "ren/core/Vm.hpp"

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
    bool success =
        ref.compare_exchange_weak(head, next, std::memory_order_acquire);
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
    // Sync with free list pop.
    bool success =
        ref.compare_exchange_weak(head, node, std::memory_order_release);
    if (success) {
      return;
    }
  }
}

} // namespace

constexpr usize THREAD_LOCAL_BIG_BLOCK_CACHE_SIZE = 8;
constexpr usize JOB_LOCAL_BIG_BLOCK_CACHE_SIZE = 4;

static thread_local bool job_is_main_thread = false;

struct alignas(FIBER_STACK_ALIGNMENT) StackFreeListNode {
  StackFreeListNode *next = nullptr;
};

enum class JobState {
  Running,
  WaitedOn,
  Done,
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

  alignas(CACHE_LINE_SIZE) u64
      big_block_free_masks[JOB_LOCAL_BIG_BLOCK_CACHE_SIZE] = {};
  void *big_blocks[JOB_LOCAL_BIG_BLOCK_CACHE_SIZE] = {};
};

struct alignas(CACHE_LINE_SIZE) QueuedJob {
  Job *job = nullptr;
};

struct alignas(CACHE_LINE_SIZE) TagBlock {
  usize size = CACHE_LINE_SIZE;
  usize offset = CACHE_LINE_SIZE;
  TagBlock *next = nullptr;
};

struct TagAllocation {
  TagBlock *head = nullptr;
};

struct JobServer {
  // Read-only data.
  usize m_page_size = 0;
  usize m_allocation_granularity = 0;
  Span<Thread> m_workers;
  Span<Thread> m_io_workers;

  // Arena mutex.
  alignas(CACHE_LINE_SIZE) Mutex m_arena_mutex;
  // Arena data.
  alignas(CACHE_LINE_SIZE) Arena m_arena;

  // Scheduler mutex.
  alignas(CACHE_LINE_SIZE) Mutex m_scheduler_mutex;
  // Scheduler data.
  alignas(CACHE_LINE_SIZE) int m_num_enqueued = 0;
  Queue<QueuedJob> m_high_priority_queue;
  Queue<QueuedJob> m_normal_priority_queue;

  alignas(CACHE_LINE_SIZE) Job *m_main_job = nullptr;
  int m_main_job_ready = false;

  alignas(CACHE_LINE_SIZE) Mutex m_io_scheduler_mutex;
  alignas(CACHE_LINE_SIZE) int m_num_io_enqueued = 0;
  Queue<QueuedJob> m_io_queue;

  // Free lists.
  alignas(CACHE_LINE_SIZE) StackFreeListNode *m_stack_free_list = nullptr;
  alignas(CACHE_LINE_SIZE) Job *m_job_free_list = nullptr;
  alignas(CACHE_LINE_SIZE)
      JobAtomicCounter *m_atomic_counter_free_list = nullptr;

  alignas(CACHE_LINE_SIZE) Mutex m_allocator_mutex;
  alignas(CACHE_LINE_SIZE) BlockAllocator m_allocator;

  alignas(CACHE_LINE_SIZE) TagBlock
      m_tail_tag_blocks[(usize)ArenaNamedTag::FirstCustom];
  alignas(CACHE_LINE_SIZE)
      TagAllocation m_tag_allocations[(usize)ArenaNamedTag::FirstCustom];
};

static JobServer job_server;

static thread_local void
    *thread_local_block_cache[THREAD_LOCAL_BIG_BLOCK_CACHE_SIZE] = {};

static void *job_allocate_big_block() {
  usize big_block_index = -1;
  for (usize i : range(THREAD_LOCAL_BIG_BLOCK_CACHE_SIZE)) {
    if (thread_local_block_cache[i]) {
      big_block_index = i;
    }
  }
  void *big_block = nullptr;
  [[likely]] if (big_block_index != (u64)-1) {
    big_block = thread_local_block_cache[big_block_index];
    thread_local_block_cache[big_block_index] = nullptr;
  } else {
    job_server.m_allocator_mutex.lock();
    big_block =
        allocate_block(&job_server.m_allocator, JOB_ALLOCATOR_BIG_BLOCK_SIZE);
    job_server.m_allocator_mutex.unlock();
  }
  return big_block;
}

static void job_free_big_block(void *big_block) {
  usize big_block_index = -1;
  for (usize i : range(THREAD_LOCAL_BIG_BLOCK_CACHE_SIZE)) {
    if (!thread_local_block_cache[i]) {
      big_block_index = i;
    }
  }
  [[likely]] if (big_block_index != (u64)-1) {
    thread_local_block_cache[big_block_index] = big_block;
    return;
  }
  job_server.m_allocator_mutex.lock();
  free_block(&job_server.m_allocator, big_block, JOB_ALLOCATOR_BIG_BLOCK_SIZE);
  job_server.m_allocator_mutex.unlock();
}

static const int WORKER_EXIT = -1;

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
  [[unlikely]] if (job_server.m_num_enqueued == WORKER_EXIT) {
    job_server.m_scheduler_mutex.unlock();
    thread_exit(EXIT_SUCCESS);
  }

  Job *job = []() {
    Optional<QueuedJob> high_priority =
        job_server.m_high_priority_queue.try_pop();
    if (high_priority) {
      return high_priority->job;
    }
    Optional<QueuedJob> normal_priority =
        job_server.m_normal_priority_queue.try_pop();
    ren_assert(normal_priority);
    return normal_priority->job;
  }();
  job_server.m_num_enqueued--;
  job_server.m_scheduler_mutex.unlock();

  return job;
}

static Job *job_schedule_from_io_queue() {
  ZoneScopedN("Schedule IO worker job");
retry:
  job_server.m_io_scheduler_mutex.lock();
  [[unlikely]] if (job_server.m_num_io_enqueued == 0) {
    job_server.m_io_scheduler_mutex.unlock();
    futex_wait(&job_server.m_num_io_enqueued, 0);
    goto retry;
  }
  [[unlikely]] if (job_server.m_num_io_enqueued == WORKER_EXIT) {
    job_server.m_io_scheduler_mutex.unlock();
    thread_exit(EXIT_SUCCESS);
  }

  Optional<QueuedJob> job = job_server.m_io_queue.try_pop();
  ren_assert(job);
  job_server.m_num_io_enqueued--;
  job_server.m_io_scheduler_mutex.unlock();

  return job->job;
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
    job_server.m_high_priority_queue.push({job});
  } else {
    ren_assert(job->priority == JobPriority::Normal);
    job_server.m_normal_priority_queue.push({job});
  }

  [[unlikely]] if (job_server.m_num_enqueued++ == 0) {
    futex_wake_one(&job_server.m_num_enqueued);
  }

  job_server.m_scheduler_mutex.unlock();
}

static void job_enqueue_to_io_queue(Job *job) {
  ZoneScoped;
  AutoMutex lock(job_server.m_io_scheduler_mutex);
  job_server.m_io_queue.push({job});
  [[unlikely]] if (job_server.m_num_io_enqueued++ == 0) {
    futex_wake_one(&job_server.m_num_io_enqueued);
  }
}

static void job_free_atomic_counter(JobAtomicCounter *counter) {
  list_remove(counter);
  // Can be read by previous owner. Released to next owner by free list push.
  std::atomic_ref(counter->generation).fetch_add(1, std::memory_order_relaxed);
  free_list_atomic_push(&job_server.m_atomic_counter_free_list, counter);
}

static void job_free(Job *job) {
  ZoneScoped;
  ren_assert(not job->is_main_job);

  bool all_done = 1 == std::atomic_ref(job->counter->value)
                           .fetch_sub(1, std::memory_order_relaxed);
  if (all_done) {
    // Release value decrement.
    JobState state = std::atomic_ref(job->counter->job_state)
                         .exchange(JobState::Done, std::memory_order_release);
    if (state == JobState::WaitedOn) {
      job_enqueue(job->parent);
    }
  }

  for (void *big_block : job->big_blocks) {
    if (big_block) {
      job_free_big_block(big_block);
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
  usize allocated_size = guard_size + stack_size + guard_size;
  u8 *new_stack = (u8 *)vm_allocate(allocated_size);
  vm_commit(new_stack, allocated_size);
  new_stack += guard_size;
  vm_protect(new_stack - guard_size, guard_size, PagePermissionNone);
  vm_protect(new_stack + stack_size, guard_size, PagePermissionNone);
  return (StackFreeListNode *)new_stack;
}

static void job_load_scheduler(JobSchedulerCommand cmd) {
  ren_assert(job_tls_running_job() or cmd == JobSchedulerCommand::Schedule);
  job_tls_set_scheduler_command(cmd);
  fiber_load_context(*job_tls_scheduler_fiber());
}

static void job_switch_to_scheduler(JobSchedulerCommand cmd) {
  Job *job = job_tls_running_job();
  ren_assert(job or cmd == JobSchedulerCommand::Schedule);
  job_tls_set_scheduler_command(cmd);
  fiber_switch_context(&job->context, *job_tls_scheduler_fiber());
}

enum class JobServerWorkerQueue {
  Default,
  IO,
};

template <JobServerWorkerQueue Q> static void job_server_worker(void *) {
  FiberContext *scheduler = job_tls_scheduler_fiber();
  *scheduler = fiber_thread_context();
  while (true) {
    JobSchedulerCommand cmd = job_tls_get_scheduler_command();
    if (cmd != JobSchedulerCommand::Schedule) {
      Job *job = job_tls_running_job();
      if (cmd == JobSchedulerCommand::Free) {
        job_free(job);
      } else if (cmd == JobSchedulerCommand::MoveToDefaultQueue) {
        job_enqueue(job);
      } else if (cmd == JobSchedulerCommand::MoveToIoQueue) {
        job_enqueue_to_io_queue(job);
      }
    }
    Job *next = nullptr;
    if constexpr (Q == JobServerWorkerQueue::Default) {
      next = job_schedule();
    } else if constexpr (Q == JobServerWorkerQueue::IO) {
      next = job_schedule_from_io_queue();
    }
    job_tls_set_running_job(next);
    fiber_switch_context(scheduler, next->context);
  }
}

static void job_wait_for_counter(Job *job, JobAtomicCounter *counter) {
  JobState state = JobState::Running;
  // Acquire value decremented in job_free.
  std::atomic_ref(counter->job_state)
      .compare_exchange_strong(state, JobState::WaitedOn,
                               std::memory_order_acquire);
  if (state != JobState::Running) {
    ren_assert(state == JobState::Done);
    return;
  }
  job_switch_to_scheduler(JobSchedulerCommand::Schedule);
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
  job_load_scheduler(JobSchedulerCommand::Free);
}

void launch_job_server() {
  ScratchArena scratch;

  job_is_main_thread = true;
  job_server = {
      .m_page_size = vm_page_size(),
      .m_allocation_granularity = vm_allocation_granularity(),
      .m_arena = Arena::init(),
      .m_high_priority_queue = Queue<QueuedJob>::init(),
      .m_normal_priority_queue = Queue<QueuedJob>::init(),
      .m_io_queue = Queue<QueuedJob>::init(),
  };
  init_allocator(&job_server.m_allocator, JOB_ALLOCATOR_BIG_BLOCK_SIZE);
  for (usize i : range(size(job_server.m_tag_allocations))) {
    job_server.m_tag_allocations[i].head = &job_server.m_tail_tag_blocks[i];
  }

  auto topology = cpu_topology(scratch);
  u32 num_cpus = 0;
  u32 max_num_cores = 0;
  for (Processor processor : topology) {
    num_cpus = max(num_cpus, processor.cpu + 1);
    max_num_cores = max(max_num_cores, processor.core + 1);
  }
  Span<bool> core_mask = Span<bool>::allocate(scratch, max_num_cores);
  u32 numa = topology[0].numa;
  for (Processor processor : topology) {
    core_mask[processor.core] = processor.numa == numa;
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

  usize job_worker_stack_size = thread_min_stack_size();
#if REN_TSAN
  job_worker_stack_size = max(256 * KiB, job_worker_stack_size);
#endif

  job_server.m_workers = Span<Thread>::allocate(&job_server.m_arena, num_cores);
  for (usize i : range(num_cores)) {
    ScratchArena scratch;

    String8 name =
        format(scratch, "Job server worker {} on core {}", i, cores[i]);
    fmt::println("job_server: Run worker {} on core {}", i, cores[i]);

    DynamicArray<u32> affinity;
    for (Processor processor : topology) {
      if (processor.numa == numa and processor.core == cores[i]) {
        affinity.push(scratch, processor.cpu);
      }
    }

    job_server.m_workers[i] = thread_create({
        .name = name.zero_terminated(&job_server.m_arena),
        .proc = job_server_worker<JobServerWorkerQueue::Default>,
        .param = nullptr,
        .stack_size = job_worker_stack_size,
        .affinity = affinity,
    });
  }

  constexpr usize NUM_CORE_IO_WORKERS = 2;
  usize num_io_workers = num_cores * NUM_CORE_IO_WORKERS;
  fmt::println("job_server: Run {} IO workers", num_io_workers);
  job_server.m_io_workers =
      Span<Thread>::allocate(&job_server.m_arena, num_io_workers);
  for (usize i : range(num_io_workers)) {
    ScratchArena scratch;
    String8 name = format(scratch, "Job server IO worker {}", i);
    job_server.m_io_workers[i] = thread_create({
        .name = name.zero_terminated(&job_server.m_arena),
        .proc = job_server_worker<JobServerWorkerQueue::IO>,
        .param = nullptr,
        .stack_size = job_worker_stack_size,
    });
  }

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

  {
    AutoMutex lock(job_server.m_scheduler_mutex);
    job_server.m_num_enqueued = WORKER_EXIT;
  }
  futex_wake_all(&job_server.m_num_enqueued);

  {
    AutoMutex lock(job_server.m_io_scheduler_mutex);
    job_server.m_num_io_enqueued = WORKER_EXIT;
  }
  futex_wake_all(&job_server.m_num_io_enqueued);

  for (Thread worker : job_server.m_workers) {
    int ret = thread_join(worker);
    ren_assert(ret == EXIT_SUCCESS);
  }

  for (Thread worker : job_server.m_io_workers) {
    int ret = thread_join(worker);
    ren_assert(ret == EXIT_SUCCESS);
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
  counter->value = jobs.m_size;
  counter->job_state = JobState::Running;
  list_insert_after(&parent->list_of_counters, counter);

  for (JobDesc job_desc : jobs) {
    Job *job = free_list_atomic_pop(&job_server.m_job_free_list);
    [[unlikely]] if (!job) {
      job_server.m_arena_mutex.lock();
      job = job_server.m_arena.allocate<Job>();
      job_server.m_arena_mutex.unlock();
    }

    usize stack_size =
        max<isize>(64 * KiB, job_server.m_allocation_granularity -
                                 2 * job_server.m_page_size);
#if REN_TSAN
    stack_size = max(256 * KiB, stack_size);
#endif
    StackFreeListNode *stack =
        free_list_atomic_pop(&job_server.m_stack_free_list);
    [[unlikely]] if (!stack) {
      // TODO: add option to set stack size, or set stack size based on
      // amount of stack space used by previous jobs with the same function
      // pointer.
      stack = job_allocate_stack(stack_size);
    }
    usize stack_reserve = 0;
    void *payload = job_desc.payload;
    if (job_desc.payload_size > 0) {
      payload = (u8 *)stack + stack_size - job_desc.payload_size;
      std::memcpy(payload, job_desc.payload, job_desc.payload_size);
      stack_reserve = (job_desc.payload_size + FIBER_STACK_ALIGNMENT - 1) &
                      ~(FIBER_STACK_ALIGNMENT - 1);
    }

    *job = {
        .parent = parent,
        .priority = parent->priority == JobPriority::High ? JobPriority::High
                                                          : job_desc.priority,
        .function = job_desc.function,
        .payload = payload,
        .counter = counter,
        .context =
            fiber_init_context(job_fiber_main, stack, stack_size, stack_reserve,
                               job_desc.label ? job_desc.label : "Untitled"),
    };
    list_init(&job->list_of_counters);
    job_enqueue(job);
  }

  // Acquired from previous owner by free list pop. Isn't written by the
  // previous owner so doesn't need to be atomic.
  u64 generation = counter->generation;
  return {counter, generation};
}

void job_wait(JobToken token) {
  // Acquired from previous owner by free list pop. Can be incremented by future
  // owner.
  u64 generation = std::atomic_ref(token.counter->generation)
                       .load(std::memory_order_relaxed);
  if (generation != token.generation) {
    return;
  }
  Job *job = job_tls_running_job();
  job_wait_for_counter(job, token.counter);
  job_free_atomic_counter(token.counter);
}

bool job_is_done(JobToken token) {
  // Acquired from previous owner by free list pop. Can be incremented by future
  // owner.
  u64 generation = std::atomic_ref(token.counter->generation)
                       .load(std::memory_order_relaxed);
  if (generation != token.generation) {
    return true;
  }

  // Acquire value decrement before delete.
  JobState state =
      std::atomic_ref(token.counter->job_state).load(std::memory_order_acquire);
  if (state == JobState::Done) {
    job_free_atomic_counter(token.counter);
    return true;
  }

  return false;
}

bool is_job() {
  Job *job = job_tls_running_job();
  return job and not job->is_main_job;
}

ArenaBlock *job_allocate_block(usize size) {
  ZoneScoped;

  Job *job = job_tls_running_job();
  [[likely]] if (size <= JOB_ALLOCATOR_BIG_BLOCK_SIZE) {
    usize num_blocks = size / JOB_ALLOCATOR_BLOCK_SIZE;
    usize big_block_index = -1;
    usize first_block = 0;
    for (usize i : range(JOB_LOCAL_BIG_BLOCK_CACHE_SIZE)) {
      u64 free_mask = job->big_block_free_masks[i];
      usize first = [&]() {
        switch (num_blocks) {
        case 1:
          return find_aligned_ones<1>(free_mask);
        case 2:
          return find_aligned_ones<2>(free_mask);
        case 4:
          return find_aligned_ones<4>(free_mask);
        case 8:
          return find_aligned_ones<8>(free_mask);
        case 16:
          return find_aligned_ones<16>(free_mask);
        case 32:
          return find_aligned_ones<32>(free_mask);
        case 64:
          return free_mask == (u64)-1 ? 0 : (u64)-1;
        }
        unreachable();
      }();
      if (first != (u64)-1) {
        big_block_index = i;
        first_block = first;
      }
    }

    [[unlikely]] if (big_block_index == (u64)-1) {
      void *big_block = job_allocate_big_block();

      // Check if this new block can be added to the job cache.
      for (usize i : range(JOB_LOCAL_BIG_BLOCK_CACHE_SIZE)) {
        if (!job->big_blocks[i]) {
          big_block_index = i;
        }
      }
      // Job cache is full. Return allocated block as is.
      [[unlikely]] if (big_block_index == (u64)-1) {
        auto *block = (ArenaBlock *)big_block;
        block->block_size = JOB_ALLOCATOR_BIG_BLOCK_SIZE;
        block->block_offset = 0;
        return block;
      }

      // Otherwise, suballocate from this new block.
      job->big_blocks[big_block_index] = big_block;
      job->big_block_free_masks[big_block_index] = -1;
    }

    u64 mask = ((u64)1 << num_blocks) - 1;
    mask = mask << first_block;
    mask = num_blocks == 64 ? (u64)-1 : mask;
    job->big_block_free_masks[big_block_index] &= ~mask;

    usize block_offset = first_block * JOB_ALLOCATOR_BLOCK_SIZE;
    auto *block =
        (ArenaBlock *)((u8 *)job->big_blocks[big_block_index] + block_offset);
    block->block_size = size;
    block->block_offset = block_offset;

    return block;
  }

  job_server.m_allocator_mutex.lock();
  auto *block = (ArenaBlock *)allocate_block(&job_server.m_allocator, size);
  job_server.m_allocator_mutex.unlock();
  block->block_size = size;
  block->block_offset = 0;

  return block;
}

void job_free_block(ArenaBlock *block) {
  Job *job = job_tls_running_job();
  [[likely]] if (block->block_size < JOB_ALLOCATOR_BIG_BLOCK_SIZE) {
    void *big_block = (u8 *)block - block->block_offset;
    usize big_block_index = -1;
    for (usize i : range(JOB_LOCAL_BIG_BLOCK_CACHE_SIZE)) {
      if (big_block == job->big_blocks[i]) {
        big_block_index = i;
      }
    }
    ren_assert(big_block_index != (u64)-1);
    usize first_block = block->block_offset / JOB_ALLOCATOR_BLOCK_SIZE;
    usize num_blocks = block->block_size / JOB_ALLOCATOR_BLOCK_SIZE;
    usize mask = ((u64)1 << num_blocks) - 1;
    mask = mask << first_block;
    job->big_block_free_masks[big_block_index] |= mask;
    return;
  }
  if (block->block_size == JOB_ALLOCATOR_BIG_BLOCK_SIZE) {
    // Try to return to job cache.
    usize big_block_index = -1;
    for (usize i : range(JOB_LOCAL_BIG_BLOCK_CACHE_SIZE)) {
      if (!job->big_blocks[i] or job->big_blocks[i] == block) {
        big_block_index = i;
      }
    }
    [[likely]] if (big_block_index != (u64)-1) {
      job->big_blocks[big_block_index] = block;
      job->big_block_free_masks[big_block_index] = -1;
      return;
    }
    job_free_big_block(block);
    return;
  }
  job_server.m_allocator_mutex.lock();
  free_block(&job_server.m_allocator, block, block->block_size);
  job_server.m_allocator_mutex.unlock();
}

void job_reset_tag(ArenaTag tag) {
  auto *tagged_allocation = &job_server.m_tag_allocations[tag.m_id];
  TagBlock *head = tagged_allocation->head;
  TagBlock *tail = &job_server.m_tail_tag_blocks[tag.m_id];
  while (head != tail) {
    TagBlock *next = head->next;
    usize block_size = head->size;
    {
      AutoMutex lock(job_server.m_allocator_mutex);
      free_block(&job_server.m_allocator, head, block_size);
    }
    head = next;
  }
  tagged_allocation->head = tail;
}

void *job_tag_allocate(ArenaTag tag, usize size, usize alignment) {
  ZoneScoped;

  ren_assert(alignment <= CACHE_LINE_SIZE);
  size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
  auto *tag_allocation = &job_server.m_tag_allocations[tag.m_id];
top:
  TagBlock *head =
      std::atomic_ref(tag_allocation->head).load(std::memory_order_acquire);
  usize offset =
      std::atomic_ref(head->offset).fetch_add(size, std::memory_order_acquire);
  [[unlikely]] if (offset + size > head->size) {
    TagBlock *new_block = nullptr;
    usize block_size =
        max(next_po2(CACHE_LINE_SIZE + size), JOB_ALLOCATOR_BIG_BLOCK_SIZE);
    {
      AutoMutex lock(job_server.m_allocator_mutex);
      new_block =
          (TagBlock *)allocate_block(&job_server.m_allocator, block_size);
    }
    new_block->size = block_size;
    new_block->offset = CACHE_LINE_SIZE + size;
    new_block->next = tag_allocation->head;
    bool swapped =
        std::atomic_ref(tag_allocation->head)
            .compare_exchange_strong(head, new_block, std::memory_order_release,
                                     std::memory_order_relaxed);
    [[unlikely]] if (not swapped) {
      AutoMutex lock(job_server.m_allocator_mutex);
      free_block(&job_server.m_allocator, new_block, block_size);
      goto top;
    }
    return (u8 *)new_block + CACHE_LINE_SIZE;
  }
  return (u8 *)head + offset;
}

void job_move_to_io_queue() {
  [[unlikely]] if (job_is_main_thread) {
    fmt::println(
        stderr,
        "job_server: warning: performing blocking IO on the main thread");
    return;
  }
  job_switch_to_scheduler(JobSchedulerCommand::MoveToIoQueue);
}

void job_move_to_default_queue() {
  [[unlikely]] if (job_is_main_thread) { return; }
  job_switch_to_scheduler(JobSchedulerCommand::MoveToDefaultQueue);
}

} // namespace ren
