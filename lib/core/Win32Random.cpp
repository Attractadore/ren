#if _WIN32
#include "Win32.hpp"
#include "ren/core/Random.hpp"

// Must come after Windows.h
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

namespace ren {

u64 sys_random() {
  u64 buf = 0;
  NTSTATUS_CHECK(BCryptGenRandom(nullptr, (u8 *)buf, sizeof(buf),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG));
  return buf;
}

} // namespace ren

#endif
