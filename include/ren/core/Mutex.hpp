#pragma once
#include "StdDef.hpp"

namespace ren {

class Mutex {
public:
  void lock();
  void unlock();

private:
  alignas(CACHE_LINE_SIZE) int m_lock_futex = 0;
};

} // namespace ren
