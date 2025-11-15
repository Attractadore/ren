#if _WIN32
#include "ren/core/Fiber.hpp"

#include <Windows.h>

namespace ren {

FiberContext fiber_thread_context() {
  FiberContext fiber = {};
  ULONG_PTR lo, hi;
  GetCurrentThreadStackLimits(&lo, &hi);
  fiber.stack_bottom = (void *)hi;
  fiber.stack_size = hi - lo;
  return fiber;
}

} // namespace ren

#endif
