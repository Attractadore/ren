#include "ren/core/Fiber.hpp"

namespace ren {

struct Job;

static thread_local Job *running_job = nullptr;
static thread_local FiberContext scheduler;
static thread_local bool is_main_thread = false;

Job *job_tls_running_job() { return running_job; }

void job_tls_set_running_job(Job *job) { running_job = job; }

FiberContext *job_tls_scheduler_fiber() { return &scheduler; }

bool job_tls_is_main_thread() { return is_main_thread; }

void job_tls_set_main_thread(bool is_main) { is_main_thread = is_main; }

} // namespace ren
