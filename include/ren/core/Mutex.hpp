#pragma once

namespace ren {

class Mutex {
public:
  void lock();
  void unlock();

private:
  int m_lock_futex = 0;
};

class AutoMutex {
public:
  AutoMutex(Mutex &mutex) {
    m_mutex = &mutex;
    m_mutex->lock();
  }

  ~AutoMutex() { m_mutex->unlock(); }

private:
  Mutex *m_mutex = nullptr;
};

} // namespace ren
