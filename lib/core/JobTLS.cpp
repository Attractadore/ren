#include "Job.hpp"

namespace ren {

static thread_local Job *running_job = nullptr;
Job *job_tls_running_job() { return running_job; }
void job_tls_set_running_job(Job *job) { running_job = job; }

static thread_local FiberContext job_scheduler;
FiberContext *job_tls_scheduler_fiber() { return &job_scheduler; }

static thread_local JobSchedulerCommand job_scheduler_command =
    JobSchedulerCommand::Schedule;

JobSchedulerCommand job_tls_get_scheduler_command() {
  return job_scheduler_command;
}

void job_tls_set_scheduler_command(JobSchedulerCommand cmd) {
  job_scheduler_command = cmd;
}

} // namespace ren
