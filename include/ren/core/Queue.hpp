#pragma once
#include "Algorithm.hpp"
#include "Arena.hpp"
#include "Math.hpp"
#include "Optional.hpp"
#include "Span.hpp"

namespace ren {

template <typename T> class Queue {
  T *m_data = nullptr;
  usize m_data_size = 0;
  usize m_page_size = 0;
  usize m_capacity = 0;
  usize m_front = 0;
  usize m_back = 0;

public:
  [[nodiscard]] static Queue init() {
    Arena arena = Arena::init();
    usize page_size = vm_page_size();
    usize capacity = max<usize>(page_size / sizeof(T), 1);
    capacity = next_po2(capacity - 1);
    usize commit_size = (capacity + page_size - 1) & ~(page_size - 1);
    ren_assert(commit_size <= arena.m_max_size);
    vm_commit(arena.m_ptr, commit_size);

    Queue queue;
    queue.m_data = (T *)arena.m_ptr;
    queue.m_data_size = arena.m_max_size;
    queue.m_page_size = page_size;
    queue.m_capacity = capacity;

    return queue;
  }

  void destroy() { vm_free(m_data, m_data_size); }

  void push(T value) {
    usize size = m_front - m_back;
    [[unlikely]] if (size == m_capacity) { expand(); }
    usize index = (m_front++) & (m_capacity - 1);
    m_data[index] = value;
  }

  Optional<T> try_pop() {
    if (m_back < m_front) {
      usize index = (m_back++) & (m_capacity - 1);
      return m_data[index];
    }
    return {};
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
