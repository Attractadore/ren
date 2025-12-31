#if __linux__
#include "Posix.hpp"
#include "ren/core/Random.hpp"

#include <sys/random.h>

namespace ren {

u64 sys_random() {
  u64 rnd;
  POSIX_CHECK(getrandom(&rnd, sizeof(rnd), 0));
  return rnd;
}

} // namespace ren
#endif
