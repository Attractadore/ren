#pragma once
#include "Algorithm.hpp"
#include "Arena.hpp"
#include "Futex.hpp"
#include "Math.hpp"
#include "Mutex.hpp"
#include "Optional.hpp"
#include "Span.hpp"

#include <atomic>

namespace ren {

template <typename T> class MpMcQueue {
  T *m_data = nullptr;
  usize m_data_size = 0;
  usize m_page_size = 0;
  usize m_capacity = 0;
  usize m_front = 0;
  usize m_back = 0;

  Mutex m_mutex;

  alignas(CACHE_LINE_SIZE) int m_availability_futex = 0;

public:
  [[nodiscard]] static MpMcQueue init() {
    Arena arena = Arena::init();
    usize page_size = vm_page_size();
    usize capacity = max<usize>(page_size / sizeof(T), 1);
    capacity = next_po2(capacity - 1);
    usize commit_size = (capacity + page_size - 1) & ~(page_size - 1);
    ren_assert(commit_size <= arena.m_max_size);
    vm_commit(arena.m_ptr, commit_size);

    MpMcQueue queue;
    queue.m_data = (T *)arena.m_ptr;
    queue.m_data_size = arena.m_max_size;
    queue.m_page_size = page_size;
    queue.m_capacity = capacity;

    return queue;
  }

  void destroy() { vm_free(m_data, m_data_size); }

  void push(T value) {
    m_mutex.lock();
    usize size = m_front - m_back;
    [[unlikely]] if (size == m_capacity) { expand(); }
    usize index = (m_front++) & ~(m_capacity - 1);
    m_data[index] = value;
    m_mutex.unlock();
    std::atomic_ref availability(m_availability_futex);
    availability.fetch_add(1, std::memory_order_relaxed);
    futex_wake_one(&m_availability_futex);
  }

  Optional<T> try_pop() {
    Optional<T> value;
    m_mutex.lock();
    if (m_back < m_front) {
      usize index = (m_back++) & ~(m_capacity - 1);
      value = m_data[index];
    }
    m_mutex.unlock();
    return value;
  }

  T pop() {
    std::atomic_ref availability(m_availability_futex);
    int count = availability.load(std::memory_order_relaxed);
    while (true) {
      if (count == 0) {
        futex_wait(&m_availability_futex, 0);
        count = availability.load(std::memory_order_relaxed);
      } else {
        bool success = availability.compare_exchange_weak(
            count, count - 1, std::memory_order_relaxed,
            std::memory_order_relaxed);
        if (success) {
          m_mutex.lock();
          ren_assert(m_back < m_front);
          usize index = (m_back++) & ~(m_capacity - 1);
          T value = m_data[index];
          m_mutex.unlock();
          return value;
        }
      }
    }
  }

private:
  void expand() {
    usize new_capacity = 2 * m_capacity;
    usize commit_size =
        (m_capacity * sizeof(T) + m_page_size - 1) & ~(m_page_size - 1);
    usize new_commit_size =
        (new_capacity * sizeof(T) + m_page_size - 1) & ~(m_page_size - 1);
    ren_assert(new_commit_size > commit_size);
    ren_assert(new_commit_size <= m_data_size);

    u8 *bytes = (u8 *)m_data;
    vm_commit(&bytes[commit_size], new_commit_size - commit_size);

    m_capacity = new_capacity;
  }
};

} // namespace ren
