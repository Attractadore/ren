#include "ren/core/Fiber.hpp"

namespace ren {

struct Job;

static thread_local Job *running_job = nullptr;
static thread_local FiberContext scheduler;

Job *job_tls_running_job() { return running_job; }

void job_tls_set_running_job(Job *job) { running_job = job; }

FiberContext *job_tls_scheduler_fiber() { return &scheduler; }

void job_tls_set_scheduler_fiber(FiberContext fiber) { scheduler = fiber; }

} // namespace ren
