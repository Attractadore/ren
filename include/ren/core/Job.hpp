#pragma once
#include "ren/core/Span.hpp"

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
};

[[nodiscard]] JobToken job_dispatch(Span<const JobDesc> jobs);

[[nodiscard]] inline JobToken job_dispatch(JobDesc job) {
  return job_dispatch({&job, 1});
}

template <typename F>
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

} // namespace ren
