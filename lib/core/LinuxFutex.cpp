#if __linux__
#include "ren/core/Assert.hpp"
#include "ren/core/Futex.hpp"

#include <linux/futex.h>
#include <sys/syscall.h>

namespace ren {

static void futex(int *location, int op, int value) {
  errno = 0;
  syscall(SYS_futex, location, op, value, (const struct timespec *)nullptr,
          (int *)nullptr, (int)0);
  // ren_assert(errno == 0);
}

void futex_wait(int *location, int value) {
  futex(location, FUTEX_WAIT_PRIVATE, value);
}

void futex_wake_one(int *location) { futex(location, FUTEX_WAKE_PRIVATE, 1); }

void futex_wake_all(int *location) {
  futex(location, FUTEX_WAKE_PRIVATE, INT_MAX);
}

} // namespace ren

#endif
