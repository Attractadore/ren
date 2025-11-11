#include "ren/core/Mutex.hpp"
#include "ren/core/Futex.hpp"

#include <atomic>

namespace ren {

void Mutex::lock() {
  std::atomic_ref lock(m_lock_futex);

  // Attempt to lock for no contention case.
  // Use compare_exchange_strong to avoid calling futex_wake_one on spurious
  // failure.
  int c = 0;
  lock.compare_exchange_strong(c, 1, std::memory_order_acquire,
                               std::memory_order_relaxed);
  if (c != 0) {
    // Tell the lock holder to wake us up.
    if (c != 2) {
      c = lock.exchange(2, std::memory_order_acquire);
    }
    while (c != 0) {
      futex_wait(&m_lock_futex, 2);
      c = lock.exchange(2, std::memory_order_acquire);
    }
  }
}

void Mutex::unlock() {
  std::atomic_ref lock(m_lock_futex);
  // Attempt to unlock for no contention case.
  if (lock.fetch_add(-1, std::memory_order_release) != 1) {
    // Some else has come: unlock and wake them up.
    lock.store(0, std::memory_order_release);
    futex_wake_one(&m_lock_futex);
  }
}

namespace ren {}

} // namespace ren
