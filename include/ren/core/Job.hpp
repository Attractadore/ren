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
  const char *label = nullptr;

public:
  template <typename T>
  [[nodiscard]] static JobDesc init(const char *label, JobPriority priority,
                                    std::invocable<T *> auto &&function,
                                    T *payload) {
    ren_assert(function);
    ren_assert(payload);
    return {
        .priority = priority,
        .function = (JobFunction *)(+function),
        .payload = (void *)payload,
        .label = label,
    };
  }

  template <typename T, std::invocable<T *> F>
  [[nodiscard]] static JobDesc init(const char *label, F &&function,
                                    T *payload) {
    return init<T>(label, JobPriority::Normal, std::forward<F>(function),
                   payload);
  }

  [[nodiscard]] static JobDesc init(const char *label, JobPriority priority,
                                    void (*function)()) {
    ren_assert(function);
    return {
        .priority = priority,
        .function = (JobFunction *)function,
        .label = label,
    };
  }

  [[nodiscard]] static JobDesc init(const char *label, void (*function)()) {
    return init(label, JobPriority::Normal, function);
  }
};

struct JobAtomicCounter;

struct JobToken {
  // TODO: add generation tracking.
  JobAtomicCounter *counter = nullptr;
};

[[nodiscard]] JobToken job_dispatch(Span<const JobDesc> jobs);

[[nodiscard]] inline JobToken job_dispatch(JobDesc job) {
  return job_dispatch({&job, 1});
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
