#if _WIN32
#include "ren/core/Assert.hpp"
#include "ren/core/Futex.hpp"

#include <Windows.h>

#pragma comment(lib, "synchronization.lib")

namespace ren {

void futex_wait(int *location, int value) {
  bool success = WaitOnAddress(location, &value, sizeof(value), INFINITE);
  ren_assert(success);
}

void futex_wake_one(int *location) { WakeByAddressSingle(location); }

void futex_wake_all(int *location) { WakeByAddressAll(location); }

} // namespace ren

#endif
