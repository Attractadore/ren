#if __linux__
#include "ren/core/Assert.hpp"
#include "ren/core/Chrono.hpp"

#include <time.h>

namespace ren {

u64 clock() {
  struct timespec time;
  int res = ::clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  ren_assert(res == 0);
  return u64(time.tv_sec) * 1'000'000'000 + time.tv_nsec;
}

} // namespace ren

#endif
