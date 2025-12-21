#pragma once
#include "ren/core/Fiber.hpp"

namespace ren {

struct Job;

Job *job_tls_running_job();
void job_tls_set_running_job(Job *job);

FiberContext *job_tls_scheduler_fiber();

enum class JobSchedulerCommand {
  Schedule,
  Free,
  MoveToDefaultQueue,
  MoveToIoQueue,
};

JobSchedulerCommand job_tls_get_scheduler_command();
void job_tls_set_scheduler_command(JobSchedulerCommand cmd);

} // namespace ren
