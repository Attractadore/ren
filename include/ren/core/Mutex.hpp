#pragma once

namespace ren {

class Mutex {
public:
  void lock();
  void unlock();

private:
  int m_lock_futex = 0;
};

} // namespace ren
