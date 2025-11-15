#include "ren/core/Fiber.hpp"

namespace ren {

void ren_fiber_start_cpp() {
  std::atomic_signal_fence(std::memory_order_acquire);
  __sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr);
}

void fiber_panic() {
  std::fputs("Tried to return from fiber\n", stderr);
  std::abort();
};

} // namespace ren
