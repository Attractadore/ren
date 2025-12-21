#pragma once
#include "ren/core/Span.hpp"
#include "ren/core/StdDef.hpp"

#include <new>
#include <utility>

namespace ren {

struct Job;

// Launch job server on main thread.
void launch_job_server();
void stop_job_server();

enum class JobPriority {
  Normal,
  High,
};

using JobFunction = void(void *);

struct JobDesc {
  JobPriority priority = JobPriority::Normal;
  JobFunction *function = nullptr;
  void *payload = nullptr;
  usize payload_size = 0;
  const char *label = nullptr;

public:
  template <std::invocable F>
    requires std::is_trivially_copyable_v<std::remove_reference_t<F>>
  [[nodiscard]] static JobDesc init(NotNull<Arena *> arena, const char *label,
                                    F &&callback) {
    void *payload = arena->allocate(sizeof(F), alignof(F));
    std::memcpy(payload, &callback, sizeof(F));
    return {
        .function =
            [](void *payload) {
              (*(const std::remove_reference_t<F> *)payload)();
            },
        .payload = payload,
        .payload_size = sizeof(F),
        .label = label,
    };
  }
};

struct JobAtomicCounter;

struct JobToken {
  JobAtomicCounter *counter = nullptr;
  u64 generation = 0;

public:
  explicit operator bool() const { return counter; }
};

[[nodiscard]] JobToken job_dispatch(Span<const JobDesc> jobs);

[[nodiscard]] inline JobToken job_dispatch(JobDesc job) {
  return job_dispatch({&job, 1});
}

template <typename F>
  requires std::same_as<std::invoke_result_t<F>, void>
[[nodiscard]] JobToken job_dispatch(const char *label, F &&callback) {
  ScratchArena scratch;
  return job_dispatch(JobDesc::init(scratch, label, std::forward<F>(callback)));
}

void job_wait(JobToken token);

bool job_is_done(JobToken token);

inline void job_dispatch_and_wait(Span<const JobDesc> jobs) {
  JobToken token = job_dispatch(jobs);
  job_wait(token);
}

inline void job_dispatch_and_wait(JobDesc job) {
  JobToken token = job_dispatch(job);
  job_wait(token);
}

ArenaTag job_new_tag();

void job_reset_tag(ArenaTag tag);

void job_free_tag(NotNull<ArenaTag *> tag);

void *job_tag_allocate(ArenaTag tag, usize size, usize alignment);

template <typename T> T *job_tag_allocate(ArenaTag tag, usize count = 1) {
  return (T *)job_tag_allocate(tag, count * sizeof(T), alignof(T));
}

template <typename T>
  requires IsTriviallyDestructible<T>
struct JobFuture {
  JobToken m_token;

public:
  JobFuture() = default;

  JobFuture(JobToken token, T *value) {
    m_token = token;
    m_value = value;
  }

  explicit operator bool() const { return m_value; };

  bool is_ready() const { return job_is_done(m_token); }

  const T &receive() const {
    job_wait(m_token);
    return *m_value;
  }

  T &receive() {
    job_wait(m_token);
    return *m_value;
  }

  T &operator*() {
    ren_assert(m_value);
    ren_assert(is_ready());
    return *m_value;
  };

private:
  T *m_value = nullptr;
};

template <typename F>
[[nodiscard]] auto job_dispatch(ArenaTag tag, const char *label, F &&callback) {
  using R = std::invoke_result_t<F>;
  R *result = job_tag_allocate<R>(tag);
  JobToken token =
      job_dispatch(label, [result, cb = std::forward<F>(callback)]() {
        new (result) R(cb());
      });
  return JobFuture<R>(token, result);
}

template <typename F>
[[nodiscard]] auto job_dispatch(NotNull<Arena *> arena, const char *label,
                                F &&callback) {
  using R = std::invoke_result_t<F>;
  R *result = arena->allocate<R>();
  JobToken token =
      job_dispatch(label, [result, cb = std::forward<F>(callback)]() {
        new (result) R(cb());
      });
  return JobFuture<R>(token, result);
}

void job_move_to_default_queue();

void job_move_to_io_queue();

class JobIoQueueScope {
public:
  JobIoQueueScope(bool active = false) {
    m_active = active;
    if (m_active) {
      job_move_to_io_queue();
    }
  }

  ~JobIoQueueScope() {
    if (m_active) {
      job_move_to_default_queue();
    }
  }

private:
  bool m_active = false;
};

constexpr usize JOB_IO_MIN_READ_SIZE = 1 * MiB;
constexpr usize JOB_IO_MIN_WRITE_SIZE = 1 * MiB;

bool is_job();

} // namespace ren
