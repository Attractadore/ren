#if __linux__
#include "ren/core/Fiber.hpp"

#include <pthread.h>

namespace ren {

FiberContext fiber_thread_context() {
  FiberContext fiber = {
      .tsan = __tsan_create_fiber(0),
  };
  __tsan_switch_to_fiber(fiber.tsan, 0);
  pthread_attr_t attr;
  pthread_getattr_np(pthread_self(), &attr);
  pthread_attr_getstack(&attr, &fiber.stack_bottom, &fiber.stack_size);
  pthread_attr_destroy(&attr);
  return fiber;
}

} // namespace ren

#endif
